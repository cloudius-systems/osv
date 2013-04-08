#include <osv/trace.hh>

struct test_object {
    int i = 1;
    long j = 2;
    static std::tuple<int, long> unpack(test_object& obj) {
        return std::make_tuple(obj.i, obj.j);
    }
};



tracepoint<unsigned, long> trace_1("tp1", "%d %d");
tracepointv<storage_args<int, long>, runtime_args<test_object&>, test_object::unpack>
    trace_2("tp2", "%d %d");


int main(int ac, char** av)
{
    test_object obj;
    trace_1(10, 20);
    trace_2(obj);
}

