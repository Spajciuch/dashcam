#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

int main()
{
    // ================== CONFIG ====================== //

    fs::path wd = "/home/orangepi/C++/dashcam_app/";

    int frame_width = 1280;
    int frame_height = 720;
    int target_fps = 30;

    std::string codec = "hevc_rkmpp";
    std::string bitrate = "5000k";

    time_t timestamp = time(NULL);
    struct tm datetime = *localtime(&timestamp);

    char timestamp_file[50];
    strftime(timestamp_file, 50, "%d.%m.%Y-%H.%M.%S.mkv", &datetime);

    fs::path filename = wd / "videos" / timestamp_file;

    // ================== CONFIG ====================== //

    cv::Mat frame;
    // cv::namedWindow("Video Player");
    cv::VideoCapture cap(0, cv::CAP_V4L2);

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    cap.set(cv::CAP_PROP_FRAME_WIDTH, frame_width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, frame_height);
    cap.set(cv::CAP_PROP_FPS, target_fps);

    target_fps = cap.get(cv::CAP_PROP_FPS);

    if (!cap.isOpened())
    {
        std::cout << "No video stream detected" << std::endl;
        system("pause");
        return -1;
    }

    std::vector<std::string> ffmpeg_command = {
        "ffmpeg",
        "-y",
        "-f", "rawvideo",
        "-pix_fmt", "bgr24",
        "-s", std::to_string(frame_width) + "x" + std::to_string(frame_height),
        "-framerate", std::to_string(target_fps),
        "-i", "-",
        "-an",
        "-fflags", "+genpts",
        "-flush_packets", "1",
        "-muxpreload", "0",
        "-muxdelay", "0",
        "-c:v", codec,
        "-b:v", bitrate,
        filename.string()};

    std::ostringstream oss;
    for (const auto &arg : ffmpeg_command)
    {
    	if (arg.find(' ') != std::string::npos)
            oss << '"' << arg << "\" ";
        else
            oss << arg << " ";
    }

    FILE *ffmpeg = popen(oss.str().c_str(), "w");
    // ========== TIMESTAMP ============ //

    int font = cv::FONT_HERSHEY_PLAIN;
    int thickness = 2;
    int line_type = cv::LINE_AA;
    double font_scale = 2;
    cv::Scalar color(255, 255, 255);
    cv::Point text_position(935, 700);

    // ========== TIMESTAMP ============ //

    while (true)
    {
        cap >> frame;
        if (frame.empty())
        {
            break;
        }

        time_t timestamp = time(NULL);
        struct tm datetime = *localtime(&timestamp);

        char time[50];
        strftime(time, 50, "%d.%m.%Y %H:%M:%S", &datetime);

        cv::putText(frame, time, text_position, font, font_scale, color, thickness, line_type);

        fwrite(frame.data, 1, frame_width * frame_height * 3, ffmpeg);
        /*imshow("Video Player", frame);

        char c = (char)cv::waitKey(25);
        if (c == 27)
        {
            break;
        }*/
    }

    cap.release();
    return 0;
}
