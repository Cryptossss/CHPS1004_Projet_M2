#include <iostream>
#include <fstream>
#if (__cplusplus >= 201703L) // C++17
#include <filesystem>
#else // C++11
#include <dirent.h>
#endif
#include <assert.h>
#include <map>
#include "sort.h"

#if (__cplusplus >= 201703L) // C++17
namespace fs = std::filesystem;
#endif

using std::cout;
using std::endl;
using std::ifstream;
using std::stoi;
using std::stof;
using std::vector;
using std::pair;
using std::string;
using std::map;
using std::tuple;

using cv::Mat;
using cv::Mat_;
using cv::Rect;
using cv::Scalar;
using cv::RNG;
using cv::Point;

using sort::Sort;

auto constexpr MAX_COLORS = 2022;
vector<Scalar> COLORS;

vector<string> split(const string& s, char delim) {
    std::istringstream iss(s);
    vector<string> ret;
    string item;
    while (getline(iss, item, delim))
        ret.push_back(item);

    return ret;
}

// (seq info, [(image, detection), ...])
tuple<map<string, string>, vector<pair<Mat, Mat>>> getInputData(string dataFolder, bool useGT=false) {
    if (*dataFolder.end() != '/') dataFolder += '/';

    ifstream ifs;
    ifs.open(dataFolder + "seqinfo.ini");
    assert(ifs.is_open());
    map<string, string> mp;
    string s;
    while (getline(ifs, s)) {
        size_t pos = s.find('=');
        if (pos != string::npos) {
            string key = s.substr(0, pos);
            string val = s.substr(pos + 1, s.size() - pos);
            mp[key] = val;
        }
    }
    ifs.close();
    assert(mp.find("imDir") != mp.end());
    assert(mp.find("frameRate") != mp.end());
    assert(mp.find("seqLength") != mp.end());

    // get file list
    vector<string> imgPaths;
#if (__cplusplus >= 201703L) // C++17
    for (const auto& entry : fs::directory_iterator(dataFolder + mp["imDir"]))
        imgPaths.push_back(entry.path());
#else // C++11
    struct dirent *entry = nullptr;
    DIR *dp = opendir( (dataFolder + mp["imDir"]).c_str() );

    while( (entry = readdir(dp)) ) {
        if( strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 )
            imgPaths.push_back( dataFolder + mp["imDir"] + "/" + entry->d_name );
    }

    closedir(dp);
#endif
    std::sort(imgPaths.begin(), imgPaths.end());
    assert(imgPaths.size() == std::stoi(mp["seqLength"]));

    vector<pair<Mat, Mat>> pairs(imgPaths.size(), {Mat(0, 0, CV_32F), Mat(0, 6, CV_32F)});

    // read images
    for (int i = 0; i < imgPaths.size(); ++i)
        pairs[i].first = cv::imread(imgPaths[i]);

    // read detections
    if (useGT) ifs.open(dataFolder + "/gt/gt.txt");
    else ifs.open(dataFolder + "/det/det.txt");
    assert (ifs.is_open());
    float x0, y0, w, h, score;
    int frameId, objId;
    while (getline(ifs, s)) {
        vector<string> ss = split(s, ',');
        frameId = stoi(ss[0]);
        objId = stoi(ss[1]);
        x0 = stof(ss[2]);
        y0 = stof(ss[3]);
        w = stof(ss[4]);
        h = stof(ss[5]);
        score = stof(ss[6]);
        Mat bbox = (Mat_<float>(1, 6) << x0 + w/2, y0 + h/2, w, h, score, 0);
        cv::vconcat(pairs[frameId-1].second, bbox, pairs[frameId-1].second);
    }
    
    return std::make_tuple(mp, pairs);
}

void draw(Mat& img, const Mat& bboxes) {
    float xc, yc, w, h, score, dx, dy;
    int trackerId;
    string sScore;
    cv::putText(img, std::to_string(bboxes.rows), Point(0, 30), cv::FONT_HERSHEY_PLAIN, 2.5, Scalar(0,0,255), 2);
    for (int i = 0; i < bboxes.rows; ++i) {
        xc = bboxes.at<float>(i, 0);
        yc = bboxes.at<float>(i, 1);
        w = bboxes.at<float>(i, 2);
        h = bboxes.at<float>(i, 3);
        dx = bboxes.at<float>(i, 6);
        dy = bboxes.at<float>(i, 7);
        trackerId = int(bboxes.at<float>(i, 8));

        cv::rectangle(img, Rect(xc - w/2, yc - h/2, w, h), COLORS[trackerId % MAX_COLORS], 2);
        cv::putText(img, std::to_string(trackerId), Point(xc - w/2, yc - h/2 - 4),
                    cv::FONT_HERSHEY_PLAIN, 1.5, COLORS[trackerId % MAX_COLORS], 2);
        cv::arrowedLine(img, Point(xc, yc), Point(xc + 5 * dx, yc + 5 * dy),
                        COLORS[trackerId % MAX_COLORS], 4);
    }
}

/**
 * Some functions developed for counting objects in a sequence of objects. Unused.
 */
void cumulative_histogram_old(const vector<pair<Mat, Mat>> &motPairs) {
    std::ofstream out("results.txt", std::ofstream::out);
    for(int i=0; i<motPairs.size(); ++i) {
        out << motPairs[i].second.rows << "\n";
    }
    out << "\n";
    out.close();
}

void cumulative_histogram(const vector<pair<Mat, Mat>> &motPairs) {
    int total = 0;
    for(int i=0; i<motPairs.size()-1; i+=2) {
        if( motPairs[i+1].second.rows > motPairs[i].second.rows )
            total += motPairs[i+1].second.rows - motPairs[i].second.rows;
    }
    cout << "total: " << total << endl;
}

void object_counting(vector<int> &ids, const Mat &bboxes) {
    // For each box detected
    for(int i=0; i<bboxes.rows; ++i) {
        if( std::count(ids.begin(), ids.end(), (int) bboxes.at<float>(i, 8) ) == 0 ) {
            //cout << (int) bboxes.at<float>(i, 8) << "\n";
            ids.push_back((int) bboxes.at<float>(i, 8));
        }
    }
}

/**
 * Main function
 */
int main(int argc, char** argv)
{   
    // generate colors
    RNG rng(MAX_COLORS);
    for (size_t i = 0; i < MAX_COLORS; ++i) {
        Scalar color = Scalar(rng.uniform(0,255), rng.uniform(0, 255), rng.uniform(0, 255));
        COLORS.push_back(color);
    }

    cout << "SORT demo" << endl;
    if (argc != 3) {
        cout << "usage: ./demo_sort [data folder] [--display={on|off}], e.g. ./demo_sort ../data/TUD-Campus/ --display=on" << endl;
        return -1;
    }
    string dataFolder = argv[1];
    // string dataFolder = "../data/TUD-Stadtmitte/";
    bool display = (strcmp(argv[2], "--display=on") == 0);
    
    // read image and detections
    cout << "Read image and detections..." << endl;    
#if (__cplusplus >= 201703L) // C++17
    auto [seqInfo, motPairs] = getInputData(dataFolder);
#else // C++11
    tuple<map<string, string>, vector<pair<Mat, Mat>>> T = getInputData(dataFolder);
    map<string, string> &seqInfo = std::get<0>(T);
    vector<pair<Mat, Mat>> &motPairs = std::get<1>(T);
#endif
    float fps = std::stof(seqInfo["frameRate"]);

    // tracking
    cout << "Tracking..." << endl;
    Sort::Ptr mot = std::make_shared<Sort>(1, 3, 0.3f);
    cv::namedWindow("SORT", cv::WindowFlags::WINDOW_NORMAL);
    float total_time = 0.0f;
    clock_t tick, tock;
    int total_box = 0;
    vector<int> ids;
#if (__cplusplus >= 201703L) // C++17
    for (auto [img, bboxesDet] : motPairs) {
#else // C++11
    for(pair<Mat, Mat> P : motPairs) {
        Mat &img = P.first;
        Mat &bboxesDet = P.second;
#endif
        tick = clock();
        Mat bboxesPost = mot->update(bboxesDet);
        tock = clock();
        total_time += (float)(tock-tick);

        total_box += bboxesPost.rows;

        object_counting(ids, bboxesPost);
        
        // show result
        if( display ) {
            draw(img, bboxesPost);
            cv::imshow("SORT", img);
            cv::waitKey(1000.0f / fps);
        }
    }
    
    cout << "Done" << endl;

    total_time /= CLOCKS_PER_SEC;
    cout << "Number of frames: " << motPairs.size() << "\n";
    cout << "Frame size: " << seqInfo["imWidth"] << "x" << seqInfo["imHeight"] << "\n";
    cout << "Total tracking time: " << total_time << " second(s)\n";
    cout << "Average tracking time per frame: " << total_time / motPairs.size() << " second(s)\n";
    cout << "Total number of tracking boxes computed: " << total_box << "\n";
    cout << "Average number of tracking boxes computed per frame: " << total_box / motPairs.size() << endl;
    cout << "Estimated number of tracked objects: " << total_box / ids.size() << endl;

    return 0;
}