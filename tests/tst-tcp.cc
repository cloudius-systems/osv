/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <boost/program_options.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <random>

using namespace std;

using std::mutex;

template <class T>
struct reverse_lock {
    reverse_lock(T& x) : x(x) {}
    void lock() { x.unlock(); }
    void unlock() { x.lock(); }
    T& x;
};

struct params {
    unsigned connections;
    unsigned concurrency;
    unsigned lifetime;
    uint64_t transfer;
    string remote;
};

class tcp_test_client {
public:
    explicit tcp_test_client(params p);
    void run();
private:
    class test_thread {
    public:
        explicit test_thread(tcp_test_client& client);
        ~test_thread();
        void run();
    private:
        void do_connection();
    private:
        tcp_test_client& _client;
        unique_lock<mutex> _lock;
        boost::asio::io_service _io;
        thread _thread;
    };
    friend class test_thread;
private:
    bool done() const;
    bool can_create_thread() const;
    void create_threads();
private:
    mutex _mtx;
    condition_variable _condvar;
    unsigned _running_threads = 0;
    unsigned _connections_completed = 0;
    params _p;
    minstd_rand _rand;
    poisson_distribution<> _dist_lifetime;
    poisson_distribution<int64_t> _dist_transfer;
    exception_ptr _ex;
    std::vector<unique_ptr<test_thread>> _completed_threads;
};

tcp_test_client::test_thread::test_thread(tcp_test_client& client)
    : _client(client)
    , _lock(_client._mtx, defer_lock)
    , _thread([=] { run(); })
{
}

tcp_test_client::test_thread::~test_thread()
{
    _thread.join();
}

void tcp_test_client::test_thread::do_connection()
{
    uint64_t transfer = _client._dist_transfer(_client._rand);
    reverse_lock<decltype(_lock)> rev_lock(_lock);
    unique_lock<decltype(rev_lock)> lock_dropper(rev_lock);
    boost::asio::ip::tcp::socket socket(_io);
    boost::asio::ip::tcp::resolver resolver(_io);
    boost::asio::ip::tcp::resolver::query query(_client._p.remote, "9999");
    auto i = resolver.resolve(query);
    if (i == decltype(resolver)::iterator()) {
        throw runtime_error("cannot resolve address");
    }
    auto endpoint = *i;
    socket.connect(endpoint);
    std::array<uint32_t, 1024> send_buffer, receive_buffer;
    uint64_t done = 0, rdone = 0;
    while (done < transfer) {
        iota(send_buffer.begin(), send_buffer.end(), done / sizeof(uint32_t));
        uint64_t offset = 0;
        auto b_size = sizeof(uint32_t) * send_buffer.size();
        auto offset_buffer = offset;
        while (done < transfer && offset < b_size) {
            auto tmp = boost::asio::buffer(send_buffer, transfer - offset_buffer) + offset;
            auto delta = socket.write_some(boost::asio::const_buffers_1(tmp));
            if (!delta) {
                throw runtime_error("short write");
            }
            offset += delta;
            done += delta;
        }
        offset = 0;
        while (rdone < transfer && offset < b_size) {
            auto tmp = boost::asio::buffer(receive_buffer, transfer - offset_buffer) + offset;
            auto delta = socket.read_some(boost::asio::mutable_buffers_1(tmp));
            if (!delta) {
                throw runtime_error("short read");
            }
            offset += delta;
            rdone += delta;
        }
        // can't use operator== since buffers may be partially full
        if (memcmp(send_buffer.data(), receive_buffer.data(), offset) != 0) {
            throw runtime_error("data mismatch");
        }
    }
    socket.close();
    lock_dropper.unlock(); // really, locks
    ++_client._connections_completed;
    if (_client.done()) {
        _client._condvar.notify_all();
    }
}

void tcp_test_client::test_thread::run()
{
    lock_guard<decltype(_lock)> guard(_lock);
    try {
        auto lifetime = _client._dist_lifetime(_client._rand);

        for (auto i = 0; i < lifetime; ++i) {
            do_connection();
        }
    } catch (...) {
        // capture the first exception
        if (!_client._ex) {
            _client._ex = current_exception();
        }
    }
    --_client._running_threads;
    _client._completed_threads.emplace_back(this);
    _client._condvar.notify_all();

}

bool tcp_test_client::can_create_thread() const
{
    return _running_threads < _p.concurrency && !done();
}

bool tcp_test_client::done() const
{
    return _connections_completed >= _p.connections || _ex;
}

void tcp_test_client::create_threads()
{
    while (can_create_thread()) {
        ++_running_threads;
        new test_thread(*this); // will queue itself in _completed_threads when done
    }
}

void tcp_test_client::run()
{
    unique_lock<mutex> lock(_mtx);

    while (!done()) {
        while (can_create_thread()) {
            create_threads();
        }
        _condvar.wait(lock);
        _completed_threads.clear();
    }
    _condvar.wait(lock, [&] { return !_running_threads; });
    _completed_threads.clear();
    if (_ex) {
        rethrow_exception(_ex);
    }
    cout << "created " << _connections_completed << " connections\n";
}

tcp_test_client::tcp_test_client(params p)
    : _p(p)
    , _dist_lifetime(p.lifetime)
    , _dist_transfer(p.transfer)
{
}

int main(int ac, char** av)
{
    namespace bpo = boost::program_options;
    params p;

    bpo::options_description desc("tst-tcp options");
    desc.add_options()
        ("help", "show help text")
        ("remote", bpo::value(&p.remote), "remote host:port")
        ("connections,n", bpo::value(&p.connections)->default_value(1000),
                "total number of connections to make")
        ("concurrency,c", bpo::value(&p.concurrency)->default_value(10),
                "maximum concurrency")
        ("lifetime,l", bpo::value(&p.lifetime)->default_value(40),
                "average thread lifetime (in connections)")
        ("transfer,t", bpo::value(&p.transfer)->default_value(10000000),
                "average transfer (per connection)")
    ;
    bpo::variables_map vars;
    bpo::store(bpo::parse_command_line(ac, av, desc), vars);
    bpo::notify(vars);

    if (vars.count("help")) {
        std::cout << desc << "\n";
        exit(1);
    }

    try {
        tcp_test_client client{p};
        client.run();
    } catch (exception& e) {
        cout << e.what() << endl;
        return 1;
    } catch (...) {
        cout << "unknown exception\n";
        return 1;
    }

    return 0;
}
