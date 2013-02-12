#ifndef __UNIT_TESTS__
#define __UNIT_TESTS__

#include <list>

namespace unit_tests {

    class vtest {
    public:
        virtual void run() = 0;
    };

    class tests {
    public:
        static tests& instance() {
            static tests i;
            return i;
        };
        void register_test(vtest* t) {tl.push_back(t);};
        void run() {for (auto i=tl.begin();i != tl.end();i++) (*i)->run();};

        static void execute_tests();

    private:
        tests() {};
        tests(tests const &t);
        void operator=(tests const &t);

        std::list<vtest*> tl;
    };
};

#endif
