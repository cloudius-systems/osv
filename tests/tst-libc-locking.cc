/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-libc-locking

#include <stdio.h>
#include <iostream>
#include <atomic>
#include <thread>
#include <future>
#include <boost/test/unit_test.hpp>

/*
 * Coordinates simultaneous thread execution release.
 */
class starting_line {
private:
	const int _n_threads;
	std::atomic<int> n_arrived;

public:
	starting_line(int n_threads) : _n_threads(n_threads)
	{
		n_arrived.store(0);
	}

	void arrive()
	{
		++n_arrived;
		assert(n_arrived.load() <= _n_threads);

		while (n_arrived.load() < _n_threads) {}
	}
};

class consumer {
private:
	std::vector<int> consumed;
	std::thread _thread;
public:
	consumer(FILE* src, starting_line& start)
		: _thread([this, src, &start] {
			start.arrive();

			int val;
			while (fread(&val, sizeof(val), 1, src) == 1) {
				consumed.push_back(val);
			}
		})
	{}

	std::vector<int> get_consumed()
	{
		return consumed;
	}

	void join()
	{
		_thread.join();
	}
};

void produce(FILE* dst, int count) {
	for (int i = 0; i < count; i++) {
		if (fwrite(&i, sizeof(i), 1, dst) == 0) {
			perror("fwrite");
			return;
		}
	}

	fclose(dst);
}

static std::vector<int> merge(std::vector<int> v1, std::vector<int> v2)
{
	std::vector<int> merged;
	merged.insert(merged.end(), v1.begin(), v1.end());
	merged.insert(merged.end(), v2.begin(), v2.end());
	return merged;
}

BOOST_AUTO_TEST_CASE(test_locking_of_file_operations)
{
	const int n_to_produce = 1000000;

	int pipefd[2];
	BOOST_REQUIRE(pipe(pipefd) == 0);
	FILE* in = fdopen(pipefd[0], "r");
	FILE* out = fdopen(pipefd[1], "w");

	starting_line starting_line(2);

	auto consumer1 = consumer(in, starting_line);
	auto consumer2 = consumer(in, starting_line);
	produce(out, n_to_produce);

	consumer1.join();
	consumer2.join();
	fclose(in);

	auto values1 = consumer1.get_consumed();
	auto values2 = consumer2.get_consumed();

	BOOST_TEST_MESSAGE("Consumer 1 collected " + std::to_string(values1.size()));
	BOOST_TEST_MESSAGE("Consumer 2 collected " + std::to_string(values2.size()));

	auto merged = merge(values1, values2);
	std::sort(merged.begin(), merged.end());

	BOOST_TEST_MESSAGE("Verifying collected data...");
	for (int i = 0; i < n_to_produce; i++) {
		BOOST_REQUIRE_EQUAL(merged[i], i);
	}
}
