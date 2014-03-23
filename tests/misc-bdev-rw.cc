#include <iostream>
#include <vector>

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/prex.h>
#include <osv/mempool.hh>

#define MB (1024*1024)

using namespace std;

atomic<int> bio_inflights(0);
atomic<bool> test_failed(false);
vector<struct bio *> done_wbio;

static void fill_buffer(void *buff, size_t len)
{
    auto data_array = static_cast<size_t *>(buff);

    for (size_t i = 0; i < len / sizeof(size_t); i++)
    {
        data_array[i] = reinterpret_cast<size_t>(&data_array[i]);
    }
}

static void rbio_done(struct bio* rbio)
{
    auto err = rbio->bio_flags & BIO_ERROR;
    auto wbio = static_cast<struct bio*>(rbio->bio_caller1);

    if (err) {
        cout << endl
             << "Read failed for " << rbio->bio_bcount << " bytes buffer"
             << endl;
        test_failed = true;
    } else {
        if (memcmp(rbio->bio_data, wbio->bio_data, rbio->bio_bcount)) {
            cout << endl
                << "data mismatch for " << rbio->bio_bcount << " bytes buffer"
                << endl;
            test_failed = true;
        } else {
            cout << ".";
        }
    }

    delete [] (char*) wbio->bio_data;
    destroy_bio(wbio);
    delete [] (char*) rbio->bio_data;
    destroy_bio(rbio);
    bio_inflights--;
}

static void wbio_done(struct bio* wbio)
{
    auto err = wbio->bio_flags & BIO_ERROR;

    if (err) {
        cout << endl
             << "Write failed for " << wbio->bio_bcount << " bytes buffer"
             << endl;
        test_failed = true;

        destroy_bio(wbio);
        bio_inflights--;
        return;
    } else {
        cout << ".";
    }

    done_wbio.push_back(wbio);
    bio_inflights--;
}

int main(int argc, char const *argv[])
{
    struct device *dev;
    if (argc < 2) {
        cout << "Usage: " << argv[0] << "<dev-name>" << endl;
        return 1;
    }

    if (device_open(argv[1], DO_RDWR, &dev)) {
        cout << "open failed" << endl;
        return 1;
    }

    long written = 0;

    //Do all writes
    for(auto i = 1; i < 32; i++)
    {
        const size_t buff_size = i * memory::page_size;

        auto bio = alloc_bio();
        bio_inflights++;
        bio->bio_cmd = BIO_WRITE;
        bio->bio_dev = dev;
        bio->bio_data = new char[buff_size];
        bio->bio_offset = written;
        bio->bio_bcount = buff_size;
        bio->bio_caller1 = bio;
        bio->bio_done = wbio_done;

        fill_buffer(bio->bio_data, buff_size);

        dev->driver->devops->strategy(bio);
        written += buff_size;
    }

    while (bio_inflights != 0) {
        usleep(2000);
    }

    //Now do all reads and verify
    while(!done_wbio.empty())
    {
        auto wbio = done_wbio.back();
        done_wbio.pop_back();

        auto rbio = alloc_bio();
        bio_inflights++;

        rbio->bio_cmd = BIO_READ;
        rbio->bio_dev = wbio->bio_dev;
        rbio->bio_data = new char[wbio->bio_bcount];
        rbio->bio_offset = wbio->bio_offset;
        rbio->bio_bcount = wbio->bio_bcount;
        rbio->bio_caller1 = wbio;
        rbio->bio_done = rbio_done;

        rbio->bio_dev->driver->devops->strategy(rbio);
    }

    while (bio_inflights != 0) {
        usleep(2000);
    }

    cout << endl
         << "Processed " << written / MB << " MB" << endl
         << "Test " << (test_failed.load() ? "FAILED" : "PASSED") << endl;

    return test_failed.load() ? 1 : 0;
}