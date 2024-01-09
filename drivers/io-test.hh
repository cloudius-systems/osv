#ifndef IO_TEST_H
#define IO_TEST_H

#include <osv/device.h>
#include <osv/bio.h>

void requesting(struct device *dev, u32 io_size, int blocksize);
void reporting(int test_duration, int report_step, int io_size);
void io_done(struct bio* bio);
void test_block_device(struct device *dev, int test_duration,int blcks_per_io, int blocksize=512, int blockshift=9);

#endif 