#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <complex>
#include <fstream>
#include <bitset>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/random.h>
#endif

#ifdef __RDRND__
#include <immintrin.h>
#endif

inline uint64_t byteswap64(uint64_t x) {
#ifdef _MSC_VER
    return _byteswap_uint64(x);
#else
    return __builtin_bswap64(x);
#endif
}

class InsaneCryptoRandomizer {
private:
    std::mt19937_64 crypto_rng;
    std::atomic<uint64_t> chaos_pool{ 0 };
    std::vector<std::thread> chaos_threads;
    bool running{ true };

    uint64_t get_rdrand() {
        uint64_t val = 0;
#ifdef __RDRND__
        for (int i = 0; i < 10; ++i) {
            if (_rdrand64_step(&val)) return val;
        }
#endif
        return val;
    }

    uint64_t get_cryptapi_random() {
        uint64_t val = 0;
#ifdef _WIN32
        BCryptGenRandom(NULL, (PUCHAR)&val, sizeof(val), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
        if (getrandom(&val, sizeof(val), 0) != sizeof(val)) {
            std::ifstream urandom("/dev/urandom", std::ios::binary);
            if (urandom) urandom.read(reinterpret_cast<char*>(&val), sizeof(val));
        }
#endif
        return val;
    }

    double quantum_chaos() {
        volatile double x = 3.14159;
        for (int i = 0; i < 100; ++i) {
            x = std::sin(x) * std::cos(x) + std::tan(x * 0.1) +
                std::exp(std::log(std::abs(x) + 0.1)) * 0.1;
        }
        return std::fmod(std::abs(x), 1.0);
    }

    uint64_t thermal_jitter() {
        uint64_t noise = 0;
        auto start = std::chrono::high_resolution_clock::now();

        volatile int dummy = 0;
        for (int i = 0; i < 10000; ++i) {
            dummy += i;
            dummy ^= dummy << 7;
            dummy ^= dummy >> 11;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        noise ^= duration.count();
        noise ^= reinterpret_cast<uint64_t>(&dummy);

        return noise;
    }

    uint64_t mandelbrot_chaos(double x, double y) {
        std::complex<double> c(x, y);
        std::complex<double> z(0, 0);
        uint64_t hash = 0;

        for (int i = 0; i < 50; ++i) {
            z = z * z + c;
            double r = std::abs(z);
            uint64_t bits;
            memcpy(&bits, &r, sizeof(r));
            hash ^= bits ^ (static_cast<uint64_t>(i) << 32);

            if (r > 2.0) {
                hash ^= 0xDEADBEEFCAFEBABEULL;
                break;
            }
        }
        return hash;
    }

    uint64_t system_entropy_sources() {
        uint64_t entropy = 0;

#ifdef _WIN32
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        entropy ^= static_cast<uint64_t>(mem.dwMemoryLoad) << 32;

        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        entropy ^= (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(snapshot, &pe)) {
                int count = 0;
                do {
                    if (count++ % 10 == 0) {
                        entropy ^= static_cast<uint64_t>(pe.th32ProcessID) ^
                            static_cast<uint64_t>(pe.cntThreads);
                    }
                } while (Process32Next(snapshot, &pe));
            }
            CloseHandle(snapshot);
        }

        POINT p;
        if (GetCursorPos(&p)) {
            entropy ^= (static_cast<uint64_t>(p.x) << 32) | p.y;
        }
#else
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            entropy ^= info.freeram ^ info.sharedram ^ info.bufferram;
            entropy ^= static_cast<uint64_t>(info.loads[0]) << 32;
        }
        entropy ^= static_cast<uint64_t>(getpid()) << 16;
        entropy ^= static_cast<uint64_t>(getppid()) << 24;
#endif

        return entropy;
    }

    class QuantumObserver {
    private:
        mutable std::atomic<uint64_t> state;
    public:
        QuantumObserver() : state(0xDEADBEEFCAFEBABEULL) {}
        uint64_t observe() {
            uint64_t observed = state.fetch_add(
                std::chrono::steady_clock::now().time_since_epoch().count(),
                std::memory_order_acq_rel
            );
            observed ^= observed << 17;
            observed ^= observed >> 13;
            return observed;
        }
    } quantum_observer;

    uint64_t cosmic_background() {
        static uint64_t counter = 0;
        counter += 0x9E3779B97F4A7C15ULL;
        counter ^= counter >> 31;
        counter *= 0xBF58476D1CE4E5B9ULL;
        counter ^= counter >> 27;
        return counter;
    }

    uint64_t get_timestamp_entropy() {
        return std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }

public:
    InsaneCryptoRandomizer() {
        uint64_t seed = get_cryptapi_random() ^
            get_rdrand() ^
            system_entropy_sources() ^
            get_timestamp_entropy() ^
            quantum_observer.observe() ^
            cosmic_background();

        std::seed_seq seq{
            static_cast<uint32_t>(seed),
            static_cast<uint32_t>(seed >> 32),
            static_cast<uint32_t>(get_cryptapi_random()),
            static_cast<uint32_t>(get_rdrand())
        };
        crypto_rng.seed(seq);

        for (int i = 0; i < 4; ++i) {
            chaos_threads.emplace_back([this, i]() {
                while (running) {
                    uint64_t local = thermal_jitter() ^
                        (reinterpret_cast<uint64_t>(this) << (i * 8)) ^
                        get_timestamp_entropy() ^
                        (static_cast<uint64_t>(quantum_chaos() * UINT64_MAX));

                    chaos_pool.fetch_xor(local, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::microseconds(rand() % 100 + 1));
                }
                });
        }
    }

    ~InsaneCryptoRandomizer() {
        running = false;
        for (auto& t : chaos_threads) {
            if (t.joinable()) t.join();
        }
    }

    uint64_t get_random() {
        uint64_t result = crypto_rng();

        result ^= chaos_pool.load(std::memory_order_relaxed);
        result ^= thermal_jitter();
        result ^= quantum_observer.observe();
        result ^= cosmic_background();

        double x = (result & 0xFFFF) / 65536.0;
        double y = ((result >> 16) & 0xFFFF) / 65536.0;
        result ^= mandelbrot_chaos(x, y);

        for (int i = 0; i < 8; ++i) {
            uint64_t even = result & 0xAAAAAAAAAAAAAAAAULL;
            uint64_t odd = result & 0x5555555555555555ULL;
            result = (even >> 1) | (odd << 1);

            result ^= result << 11;
            result ^= result >> 23;
            result ^= result << 19;

            if (result & (1ULL << i)) {
                result = byteswap64(result);
            }
        }

        return result;
    }

    template<typename T>
    T get_in_range(T min, T max) {
        if (min > max) std::swap(min, max);
        if constexpr (std::is_floating_point_v<T>) {
            double r = static_cast<double>(get_random()) / UINT64_MAX;
            return min + static_cast<T>(r * (max - min));
        }
        else {
            uint64_t range = static_cast<uint64_t>(max - min) + 1;
            uint64_t r;
            do {
                r = get_random();
            } while (r > UINT64_MAX - (UINT64_MAX % range));
            return min + static_cast<T>(r % range);
        }
    }

    uint8_t get_byte() {
        return static_cast<uint8_t>(get_random() & 0xFF);
    }

    double get_double() {
        return static_cast<double>(get_random()) / UINT64_MAX;
    }

    std::string get_string(size_t length) {
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += static_cast<char>(get_in_range(32, 126));
        }
        return result;
    }

    void print_random_color(const std::string& text) {
#ifdef _WIN32
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        SetConsoleTextAttribute(hConsole, get_in_range(1, 15));
        std::cout << text;
        SetConsoleTextAttribute(hConsole, csbi.wAttributes);
#else
        std::cout << "\033[38;5;" << get_in_range(0, 255) << "m" << text << "\033[0m";
#endif
    }
};

int main() {
    InsaneCryptoRandomizer random;

    std::cout << "Randimer (1-100): ";
    for (int i = 0; i < 20; ++i) {
        std::cout << random.get_in_range(1, 100) << " ";
    }

    return 0;
}
