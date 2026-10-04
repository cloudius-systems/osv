/* Copyright (C) 2026 Greg Burd. */
#pragma once
#include <mutex>
using mutex = std::mutex;
#define WITH_LOCK(m) switch (0) case 0: default: if (std::unique_lock<mutex> guard{m}; false) {} else
