/* Copyright (C) 2026 Greg Burd. */
// Actual-client host driver. Only OSv scheduling/logging is shimmed.
#include "crucible-client.hh"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <cerrno>
#include <cstdlib>
#include <new>
#include <memory>
std::atomic<int> fail_sends{0};
std::atomic<int> stalled_fd{-2};
std::atomic<bool> write_sent{false};
std::atomic<bool> stalled_shutdown{false};
extern "C" int __real_shutdown(int,int);
extern "C" int __wrap_shutdown(int fd,int how) {
    if (fd == stalled_fd) stalled_shutdown=true;
    return __real_shutdown(fd,how);
}
thread_local int fail_payload_allocation = 0;
void* operator new(size_t size) {
    if (size == 512 && fail_payload_allocation && --fail_payload_allocation == 0) {
        for (int i=0; i<1000 && !write_sent; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        throw std::bad_alloc();
    }
    if (auto ptr=std::malloc(size)) return ptr;
    throw std::bad_alloc();
}
__attribute__((noinline)) void operator delete(void* ptr) noexcept { std::free(ptr); }
__attribute__((noinline)) void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
std::atomic<bool> delay_connect{false};
extern "C" int __real_connect(int, const sockaddr*, socklen_t);
extern "C" int __wrap_connect(int fd, const sockaddr* addr, socklen_t len) {
    if (delay_connect) std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return __real_connect(fd, addr, len);
}
// Admission regression faults stay below the real Connection/client logic.
std::atomic<size_t> admission_filled{0};
std::atomic<bool> admission_blocked{false};
int admission_fd = -1; // only the admission owner calls send here
extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" ssize_t __wrap_send(int fd, const void* buf, size_t len, int flags) {
    const char* fault = std::getenv("CRUCIBLE_ADMISSION_FAULT");
    if (fault && admission_fd == -1 && len >= 8 &&
        static_cast<const unsigned char*>(buf)[4] == 5) {
        admission_fd = fd;
    }
    if (fault && fd == admission_fd) {
        if (!std::strcmp(fault, "backpressure")) {
            int size = 4096;
            if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) std::abort();
            char padding[4096]{};
            // The peer deliberately stops reading. Fill the actual TCP buffers;
            // do not synthesize EAGAIN on an otherwise writable descriptor.
            while (admission_filled < 16 * 1024 * 1024) {
                auto n = __real_send(fd, padding, sizeof(padding), flags);
                if (n > 0) { admission_filled += n; continue; }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    admission_blocked = true;
                    break;
                }
                std::abort();
            }
            if (!admission_blocked) std::abort();
        }
        if (!std::strcmp(fault, "eintr")) { errno=EINTR; return -1; }
        if (!std::strcmp(fault, "progress")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return __real_send(fd, buf, 1, flags);
        }
    }
    if (len >= 8 && static_cast<const unsigned char*>(buf)[4] == 30) write_sent=true;
    int unset=-1; stalled_fd.compare_exchange_strong(unset, fd);
    if (stalled_fd == fd) { errno=EAGAIN; return -1; }
    if (fail_sends.load() && fail_sends.fetch_sub(1) > 0) { errno=EPIPE; return -1; }
    return __real_send(fd,buf,len,flags);
}

extern "C" int kprintf(const char* fmt, ...) {
    va_list ap; va_start(ap,fmt); int r=vfprintf(stderr,fmt,ap); va_end(ap); return r;
}
int main(int argc, char** argv) {
    if(argc != 4) return 2;
    crucible::Uuid uuid{};
    crucible::UpsairsClient client({argv[1],argv[2],argv[3]},uuid,512,0,false,false,1);
    std::unique_ptr<crucible::UpsairsClient> reopened;
    char line[80]; unsigned char data[512];
    setvbuf(stdout,nullptr,_IONBF,0);
    while(fgets(line,sizeof(line),stdin)) {
        try {
            if(line[0]=='a') {
                delay_connect=true;
                std::thread connector([&] { try { client.connect(); } catch (...) {} });
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                client.disconnect(); connector.join();
                printf("admission-stopped %d\n",!client.is_connected());
            }
            if(line[0]=='x') {
                std::thread first([&] { client.disconnect(); });
                client.disconnect(); first.join(); puts("both-stopped");
            }
            if(line[0]=='c') { client.connect(); puts("connected"); }
            if(line[0]=='j') printf("admission-pressure %zu %d\n",admission_filled.load(),bool(admission_blocked));
            if(line[0]=='t') {
                std::thread connector([&] { try { client.connect(); } catch (...) {} });
                auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(6);
                while (!admission_blocked && std::chrono::steady_clock::now()<limit)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto start=std::chrono::steady_clock::now();
                client.disconnect(); connector.join();
                auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-start).count();
                printf("admission-cancel %d %ld\n",!client.is_connected(),ms);
            }
            if(line[0]=='g') {
                client.disconnect();
                reopened.reset(new crucible::UpsairsClient({argv[1],argv[2],argv[3]},uuid,512,0,false,false,std::stoull(line+1)));
                reopened->connect(); puts("reopened");
            }
            if(line[0]=='R') { memset(data,0,sizeof(data)); int rc=reopened->read_sync(0,512,data); printf("read %d %u\n",rc,data[0]); }
            if(line[0]=='r') { memset(data,0,sizeof(data)); int rc=client.read_sync(0,512,data); printf("read %d %u\n",rc,data[0]); }
            if(line[0]=='w') { memset(data,66,sizeof(data)); printf("write %d\n",client.write_sync(0,512,data)); }
            if(line[0]=='f') printf("flush %d\n",client.flush_sync());
            if(line[0]=='s') printf("snapshot %d\n",client.create_snapshot(42));
            if(line[0]=='o') {
                int results[4]; unsigned char rd[512], wr[512]; memset(wr,66,sizeof(wr));
                std::thread r([&] { results[0]=client.read_sync(0,512,rd); });
                std::thread w([&] { results[1]=client.write_sync(0,512,wr); });
                std::thread f([&] { results[2]=client.flush_sync(); });
                std::thread s([&] { results[3]=client.create_snapshot(43); });
                r.join(); w.join(); f.join(); s.join();
                printf("overlap %d %d %d %d\n",results[0],results[1],results[2],results[3]);
            }
            if(line[0]=='v') printf("backlog-quarantined %d\n",bool(stalled_shutdown));
            if(line[0]=='b') { stalled_fd=-1; puts("backpressure"); }
            if(line[0]=='n') {
                fail_payload_allocation=2; write_sent=false; memset(data,66,sizeof(data));
                try { client.write_sync(0,512,data); } catch (const std::bad_alloc&) {}
                fail_payload_allocation=0;
                printf("allocation-fenced %d sent %d\n",!client.is_connected(),bool(write_sent));
            }
            if(line[0]=='e') { fail_sends=3; puts("armed"); }
            if(line[0]=='h') {
                std::thread reader([&] { client.read_sync(0,512,data); });
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                client.disconnect(); reader.join(); puts("stopped");
            }
            if(line[0]=='i') printf("online %d\n",client.is_connected());
            if(line[0]=='d') { client.disconnect(); puts("disconnected"); }
            if(line[0]=='q') break;
        } catch(const std::exception& e) { printf("error %s\n",e.what()); }
    }
}
