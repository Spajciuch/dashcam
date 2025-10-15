#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <atomic>
#include <csignal>
#include <condition_variable>
#include <mutex>
#include <wiringPi.h>

namespace fs = std::filesystem;

std::atomic<pid_t> dashcam_pid{-1};
std::atomic<bool> recording{false};
std::atomic<bool> exit_thread{false};
std::condition_variable cv;
std::mutex cv_m;

struct dir_info
{
    uint64_t size;
    int file_count;
};

dir_info dir_size(std::string path, bool autoremove);

auto interruptable_sleep = [](int seconds)
{
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, std::chrono::seconds(seconds), []
                { return exit_thread.load(); });
};

void remove_oldest_file(fs::path &wd, uint64_t max_size, int max_day = 7)
{
    while (!exit_thread)
    {
        if (dir_size(wd, true).size >= max_size)
        {
            std::vector<fs::directory_entry> files;

            for (auto &entry : fs::directory_iterator(wd))
            {
                if (fs::is_regular_file(entry))
                {
                    files.push_back(entry);
                }
            }

            if (files.empty())
            {
                std::cout << "[watchdog] Brak plików do usunięcia" << std::endl;
                return;
            }

            auto oldest = *std::min_element(files.begin(), files.end(),
                                            [](const fs::directory_entry &a, const fs::directory_entry &b)
                                            {
                                                return fs::last_write_time(a) < fs::last_write_time(b);
                                            });

            auto ftime = fs::last_write_time(oldest);

            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

            auto age = std::chrono::duration_cast<std::chrono::hours>(
                           std::chrono::system_clock::now() - sctp)
                           .count() /
                       24;

            if (age < max_day)
            {
                std::cout << "[watchdog] Najstarszy plik ma mniej niż " << max_day << " dni, nie usuwam" << std::endl;
                interruptable_sleep(30);
                continue;
            }

            std::cout << "[watchdog] Usuwam: " << oldest.path() << std::endl;
            try
            {
                bool status = fs::remove(oldest.path());

                if (status)
                    std::cout << "[watchdog] Usunięto najstarszy plik" << std::endl;
            }
            catch (const fs::filesystem_error &e)
            {
                std::cout << "[watchdog] Błąd podczas usuwania" << std::endl;
            }
        }

        interruptable_sleep(30);
    }
}

void signal_recording(int led_pin)
{
    while (!exit_thread)
    {
        if (recording)
        {
            digitalWrite(led_pin, HIGH);
            interruptable_sleep(1);
            digitalWrite(led_pin, LOW);
            interruptable_sleep(1);
        }

        else if (!recording)
        {
            digitalWrite(led_pin, LOW);
            interruptable_sleep(2);
        }
    }
}

void monitor_power(int m_pin, int s_pin)
{
    digitalWrite(s_pin, HIGH);

    while (!exit_thread)
    {
        if (!digitalRead(m_pin))
        {
            interruptable_sleep(2); // zapłon zaraz po zgaśnięciu

            if (dashcam_pid > 0 && !digitalRead(m_pin))
                kill(dashcam_pid, SIGTERM);
            interruptable_sleep(2);
            // digitalWrite(test_acc, LOW);
        }
        else
            digitalWrite(s_pin, HIGH);
        interruptable_sleep(1);
    }
}

void run_dashcam(const fs::path &dashcamPath)
{
    while (!exit_thread)
    {
        dashcam_pid = fork();
        if (dashcam_pid == 0)
        {
            recording = false;
            execl(dashcamPath.c_str(), dashcamPath.filename().c_str(), (char *)nullptr);
            _exit(1);
        }

        else if (dashcam_pid > 0)
        {
            recording = true;
            std::cout << "[watchdog] Uruchomiono dashcam PID: " << dashcam_pid << "\n";

            int status;
            while (!exit_thread)
            {
                pid_t result = waitpid(dashcam_pid, &status, WNOHANG);
                if (result != 0)
                {
                    recording = false;
                    std::cout << "[watchdog] Proces dashcam zakończył się z kodem: " << status << "\n";
                    break;
                }

                interruptable_sleep(1);
            }
        }
        else
        {
            std::cerr << "[watchdog] Fork nie powiódł się!\n";
            interruptable_sleep(5);
        }

        interruptable_sleep(5);
    }
}

dir_info dir_size(std::string path, bool autoremove)
{
    uint64_t size = 0;
    int file_count = 0;

    for (auto &p : fs::recursive_directory_iterator(path))
    {
        size += (fs::file_size(p.path()));
        if (autoremove && fs::file_size(p.path()) <= 25 * 1024 * 1024)
        {
            try
            {
                auto ftime = fs::last_write_time(p.path());
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

                auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now() - sctp)
                                       .count();

                if (age_seconds >= 300)
                {
                    try
                    {
                        fs::remove(p.path());
                        std::cout << "[watchdog] Usunięto: " << p.path() << std::endl;
                    }
                    catch (const fs::filesystem_error &e)
                    {
                        std::cerr << "[watchdog] Błąd przy usuwaniu pliku "
                                  << p.path() << ": " << e.what() << std::endl;
                    }
                }
            }
            catch (const fs::filesystem_error &e)
            {
                std::cerr << "[watchdog] Błąd przy usuwaniu pliku "
                          << p.path() << ": " << e.what() << std::endl;
            }
        }
        else
            file_count++;
    }

    return {size, file_count};
}

void soft_exit(int signum)
{
    std::cout << "[watchdog] Zamykanie programu..." << std::endl;
    recording = false;

    {
        std::lock_guard<std::mutex> lock(cv_m);
        exit_thread = true;
    }

    cv.notify_all();
}

int main()
{
    std::signal(SIGTERM, soft_exit);
    std::signal(SIGINT, soft_exit);

    fs::path wd = "/home/orangepi/C++/dashcam_app/dashcam";
    fs::path videos_path = "/home/orangepi/C++/dashcam_app/videos";

    fs::path dashcamScript = wd;
    uint64_t max_size = 50ull * 1024 * 1024 * 1024;

    int led_pin = 14;
    int m_pin = 25;
    int s_pin = 23;

    if (wiringPiSetup() == -1)
    {
        std::cerr << "[dashcam] Nie można zainicjalizować wiringOP" << std::endl;
    }

    pinMode(s_pin, OUTPUT);
    pinMode(m_pin, INPUT);
    pinMode(led_pin, OUTPUT);

    std::thread t1(monitor_power, m_pin, s_pin);
    std::thread t_space(remove_oldest_file, std::ref(videos_path), max_size, 7);
    std::thread s_rec(signal_recording, led_pin);

    while (!exit_thread)
    {
        run_dashcam(dashcamScript);
        interruptable_sleep(1);
    }

    if (t1.joinable())
        t1.join();
    if (t_space.joinable())
        t_space.join();
    if (s_rec.joinable())
        s_rec.join();

    return 0;
}