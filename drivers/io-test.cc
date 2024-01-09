#include "drivers/io-test.hh"
#include <osv/contiguous_alloc.hh>
#include <stdio.h>
#include <stdlib.h>
#include <random>
#include <machine/atomic.h>
#include <osv/clock.hh>

volatile bool running;
volatile u64 completed_io;
volatile u64 requested_io;
std::atomic<unsigned int> open_req;
u32 max_open;
u64 max_ios;

void test_block_device(struct device *dev, int test_duration, int blcks_per_io, int blocksize, int blockshift)
{
    int report_step = 1e6;
    int io_size = blocksize * blcks_per_io;
    completed_io = 0;
    requested_io = 0;
    open_req.store(0);
    max_open = 64;
    max_ios = 1 << 30;
    
    printf("Start IO test dev : %s, IO size : %d\n",dev->name,io_size);
    sched::thread *t;
    t = sched::thread::make([dev,io_size,blockshift] { requesting(dev,io_size,blockshift);},
        sched::thread::attr().name("IO_Test Request"));

    sched::thread *timer;
    timer = sched::thread::make([test_duration] { usleep(test_duration);},
        sched::thread::attr().name("IO_Test_Timer"));

    sched::thread *repo;
    repo = sched::thread::make([test_duration,report_step,io_size] { reporting(test_duration,report_step,io_size);},
        sched::thread::attr().name("IO_Test_Timer"));
    auto c = clock::get();

    running = true;
    u64 start = c->time();
    timer->start();
    t->start();
    repo->start();

    timer->join();
    running = false;
    u64 com = completed_io; 
    u64 end = c->time();
    int iops = (com * 1e9)/ (end - start);

    t->join();
    repo->join();
    printf("Test results runtime: %llu, completed IO : %llu, IOPS : %d\n",end-start,com,iops);
}

void reporting(int test_duration, int report_step, int io_size) {
    u32 prev_compl = completed_io;
    u32 compl_diff;
    u32 compl_tem;
    auto c = clock::get();
    int time_diff;
    int time_tem;
    int prev_time = c->time();
    while(running) {
        usleep(report_step);
        compl_tem = completed_io;
        time_tem = c->time();

        compl_diff = compl_tem - prev_compl;
        prev_compl = compl_tem;
        time_diff = time_tem - prev_time;
        prev_time = time_tem;
        double iops = (compl_diff * 1e9 ) / (double) time_diff;

        printf("Timestep: %d, completed : %d, IOPS : %lf, open : %d\n",time_diff,compl_diff,iops,open_req.load());
    }
}


void requesting(struct device *dev, u32 io_size, int blockshift) {
    void* buff;
    bio* bio;
    off_t max_blocks = dev->size >> blockshift;
    off_t max_offset = (max_blocks - 1) - (io_size >> blockshift);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, max_offset);

    while(running) {
        if(requested_io >= max_ios )
            break;

        buff = memory::alloc_phys_contiguous_aligned(io_size,2);
        assert(buff);
        memset(buff, 1, io_size);

        bio = alloc_bio();
        bio->bio_dev = dev;
        bio->bio_data = buff;
        bio->bio_done = io_done;
        bio->bio_length = io_size;
        bio->bio_bcount = io_size;
        bio->bio_cmd = BIO_READ;
        
        bio->bio_offset = ((off_t) distrib(gen)) << blockshift;

        while(max_open<=open_req) {
            usleep(10);
        }
        open_req.fetch_add(1);
        atomic_add_64(&requested_io,1);
        dev->driver->devops->strategy(bio);
    }
}

void io_done(struct bio* bio) {
    
    if(bio->bio_flags != BIO_DONE) {
        printf("BIO_Error during IO Test: %x\n",bio->bio_flags);
    }
    u64 old = atomic_fetchadd_long(&completed_io, 1);
    
    open_req.fetch_add(-1);

    free(bio->bio_data);
    delete bio;
}