#include <iostream>
#include <fstream>

#include <vpg.h>

#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
#include <dlib/opencv.h>

#include "facetracker.h"

template <typename T>
std::string num2str(T value, unsigned char precision=1);

void render_face_shape (cv::Mat &img, const dlib::full_object_detection& d);

void drawDataWindow(const cv::String &_title, const cv::Size _windowsize, const std::vector<const float *> _data, const int _datalength, float _ymax, float _ymin, const std::vector<cv::Scalar> &_colors);

void selectRegionByMouse(int event, int x, int y, int flags, void* userdata);

void loadSelection(void* userdata);

void saveSelection(void* userdata);

const cv::String keys = "{help h          |                 | - print help}"
                        "{device d        |       0         | - video capture device enumerator}"
                        "{facesize        |      256        | - horizontal size of the face region}"
                        "{outputfilename o|    facevpg.csv  | - output filename that contains all the measurements}"
                        "{colorchannel c  |     green       | - color channel that should be used to compute vpg signal, allowed names: red, green, blue. You also be able to toggle color channels interctively by r, g, b keys}";

int main(int argc, char *argv[])
{
    cv::CommandLineParser cmdargsparser(argc, argv, keys);
    cmdargsparser.about(cv::String(APP_NAME) + " v." + cv::String(APP_VERSION));
    if(cmdargsparser.has("help"))   {
       cmdargsparser.printMessage();
       std::cout << "Designed by " << APP_DESIGNER << std::endl;
       return 0;
    }

    // Open file for output
    std::ofstream ofs;
    ofs.open(cmdargsparser.get<std::string>("outputfilename"));
    if(ofs.is_open() == false) {
        std::cout << "Can not open file '" << cmdargsparser.get<std::string>("outputfilename") << "' for write! Abort..." << std::endl;
        return 1;
    }

    // Facetracker
    std::cout << "Loading resources from HDD. Please wait..." << std::endl;
    const cv::String facecascadefilename = "haarcascade_frontalface_alt2.xml";
    cv::CascadeClassifier facedet(facecascadefilename);
    if(facedet.empty()) {
        std::cout << "Could not load face detector resources! Abort..." << std::endl;
        return 2;
    }
    const cv::String eyecascadefilename = "haarcascade_eye.xml";
    cv::CascadeClassifier eyedet(eyecascadefilename);
    if(eyedet.empty()) {
        std::cout << "Could not load eye detector resources! Abort..." << std::endl;
        return 3;
    }
    FaceTracker facetracker(11, FaceTracker::FaceShapeOpencv);
    facetracker.setFaceClassifier(&facedet);
    facetracker.setEyeClassifier(&eyedet);

    // Video capture
    cv::VideoCapture videocapture;
    if(videocapture.open(cmdargsparser.get<int>("device")) == false) {       
        std::cerr << "Can not open video device # " << cmdargsparser.get<int>("device") << "! Abort..." << std::endl;
        return 4;
    }

    // Dlib's stuff
    dlib::shape_predictor dlibshapepredictor;
    try {
        dlib::deserialize("shape_predictor_68_face_landmarks.dat") >> dlibshapepredictor;
    }
    catch(...) {
        std::cerr << "Can not load dlib's resources!" << std::endl;
        return 5;
    }
    facetracker.setFaceShapeDetector(&dlibshapepredictor);

    // Opencv face marks
    cv::Ptr<cv::face::Facemark> facemark = cv::face::createFacemarkLBF();
    facemark->loadModel("lbfmodel.yaml");
    if(facemark->empty()) {
        std::cerr << "Can not load 'lbfmodel.yaml'!" << std::endl;
        return 6;
    }
    facetracker.setFaceMarker(facemark);

    // VPGLIB's stuff
    vpg::FaceProcessor faceproc(facecascadefilename);
    std::cout << "Measuring frame period. Please wait..." << std::endl;
    float framePeriod = faceproc.measureFramePeriod(&videocapture);
    std::cout << framePeriod << " ms" << std::endl;

    std::vector<std::vector<cv::Scalar>> _vvc;
    std::vector<cv::Scalar> _colors;
    // Red
    _colors.push_back(cv::Scalar(0,127,255));
    _colors.push_back(cv::Scalar(0,0,255));
    _vvc.push_back(_colors);
    _colors.clear();
    //Green
    _colors.push_back(cv::Scalar(0,255,255));
    _colors.push_back(cv::Scalar(0,255,0));
    _vvc.push_back(_colors);
    _colors.clear();
    //Blue
    _colors.push_back(cv::Scalar(255,127,0));
    _colors.push_back(cv::Scalar(255,127,127));
    _vvc.push_back(_colors);

    std::string _colorchannelstr = cmdargsparser.get<std::string>("colorchannel");
    size_t _colorset = 0;
    if(_colorchannelstr.compare("red") == 0) {
        _colorset = 0;
    } else if(_colorchannelstr.compare("green") == 0) {
        _colorset = 1;
    } else if(_colorchannelstr.compare("blue") == 0) {
        _colorset = 2;
    }

    vpg::PulseProcessor pulseprocfirst(framePeriod), pulseprocsecond(framePeriod);
    std::vector<const float *> _vpgsignals;
    _vpgsignals.push_back(pulseprocfirst.getSignal());
    _vpgsignals.push_back(pulseprocsecond.getSignal());
    // Add peak detector for the cardio intervals evaluation and analysis
    int totalcardiointervals = 25;
    vpg::PeakDetector peakdetfirst(pulseprocfirst.getLength(), totalcardiointervals, 11, framePeriod), peakdetsecond(pulseprocsecond.getLength(), totalcardiointervals, 11, framePeriod);
    pulseprocfirst.setPeakDetector(&peakdetfirst);
    pulseprocsecond.setPeakDetector(&peakdetsecond);
    std::vector<const float *> _vhrvsignals;
    _vhrvsignals.push_back(peakdetfirst.getIntervalsVector());
    _vhrvsignals.push_back(peakdetsecond.getIntervalsVector());
    // Create local variables to store frame and processing values
    float _hrupdateIntervalms = 0.0;
    float _r = 0.0, _g = 0.0, _b = 0.0, t = 0.0;
    float _dummytime = 0.0; // it is dummy variable, it is needed because we do not want to modify frame time on the second frame processing call
    std::pair<unsigned int, unsigned int> _hr(pulseprocfirst.getFrequency(),pulseprocsecond.getFrequency());
    std::pair<float, float> _snr(0.0,0.0);
    float _avgBlue[] = {0,0,0,0}, _avgGreen[] = {0,0,0,0}, _avgRed[] = {0,0,0,0};

    cv::Mat frame, faceregion;
    cv::Size targetfacesize(cmdargsparser.get<int>("facesize"), cmdargsparser.get<int>("facesize") * 1.33);

    cv::namedWindow("Select regions");
    //set the callback function for any mouse event
    std::pair<cv::Rect,cv::Rect> _selectionpair = std::make_pair(cv::Rect(), cv::Rect());

    // Prepare output -------------------------------------------------------------------------------------------------------------
    std::cout << "Measurements will be written to '" << cmdargsparser.get<std::string>("outputfilename") << "'" << std::endl;
    std::time_t _timet = std::time(0);
    struct std::tm * now = localtime( &_timet );
    ofs << "This file has been created by " << APP_NAME << " v." << APP_VERSION << std::endl;
    ofs << "Record was started at "
        << std::setw(2) << std::setfill('0') << now->tm_mday << '.'
        << std::setw(2) << std::setfill('0') << (now->tm_mon + 1) << '.'
        << (now->tm_year + 1900) << ' '
        << std::setw(2) << std::setfill('0') << (now->tm_hour) << ":"
        << std::setw(2) << std::setfill('0') << (now->tm_min) << ":"
        << std::setw(2) << std::setfill('0') << now->tm_sec << std::endl
        << "Measurement time interval " << framePeriod << "[ms]" << std::endl
        << "Face scheme: |3|0|" << std::endl
        << "             |2|1|" << std::endl
        << "r[0],\tg[0],\tb[0],\tr[1],\tg[1],\tb[1],\tr[2],\tg[2],\tb[2],\tr[3],\tg[3],\tb[3]"
        << ",\tR_C1,\tG_C1,\tB_C1,\tR_C2,\tG_C2,\tB_C2,\tVPG_C1,\tVPG_C2";
    for(unsigned int k = 0; k < 68; ++k)
        ofs << ",\tP[" << k << "].X,\tP[" << k << "].Y";
    ofs << std::fixed << std::setprecision(3) << std::endl;
   // -----------------------------------------------------------------------------------------------------------------------------

    std::cout << "You can select regions you want to process on the window 'Select regions'. Use your mouse..." << std::endl;
    cv::setMouseCallback("Select regions", selectRegionByMouse, &_selectionpair);
    loadSelection(&_selectionpair);
    while(videocapture.read(frame)) {

        faceregion = facetracker.getResizedFaceImage(frame,targetfacesize);
        if(!faceregion.empty()) {
           faceproc.enrollFace(faceregion,_avgRed,_avgGreen,_avgBlue,t);
           for(unsigned int _part = 0; _part < 4; ++_part) {
               ofs << _avgRed[_part] << ",\t" << _avgGreen[_part] << ",\t" << _avgBlue[_part] << ",\t";
           }

           faceproc.enrollImagePart(faceregion,_r,_g,_b,_dummytime,_selectionpair.first);
           ofs << _r << ",\t" << _g << ",\t" << _b << ",\t";
           switch(_colorset) {
                case 0:
                    pulseprocfirst.update(_r,t);
                    break;
               case 1:
                   pulseprocfirst.update(_g,t);
                   break;
               case 2:
                   pulseprocfirst.update(_b,t);
                   break;
           }
           faceproc.enrollImagePart(faceregion,_r,_g,_b,_dummytime,_selectionpair.second);
           ofs << _r << ",\t" << _g << ",\t" << _b << ",\t";
           switch(_colorset) {
                case 0:
                    pulseprocsecond.update(_r,t);
                    break;
               case 1:
                   pulseprocsecond.update(_g,t);
                   break;
               case 2:
                   pulseprocsecond.update(_b,t);
                   break;
           }
           _hrupdateIntervalms += t;
           if(_hrupdateIntervalms > 1500.0) {
               _hr.first = (_hr.first + pulseprocfirst.computeFrequency() + (60000.0 / peakdetfirst.averageCardiointervalms())) / 3.0;
               _snr.first = pulseprocfirst.getSNR();
               _hr.second = (_hr.second + pulseprocsecond.computeFrequency() + (60000.0 / peakdetsecond.averageCardiointervalms())) / 3.0;
               _snr.second = pulseprocsecond.getSNR();
               _hrupdateIntervalms = 0.0;
           }
           std::string _hrstr = "HR: " + std::to_string(_hr.first) + " bpm";
           cv::putText(frame, _hrstr, cv::Point(4,20), cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(0,0,0),1,cv::LINE_AA);
           cv::putText(frame, _hrstr, cv::Point(3,19), cv::FONT_HERSHEY_SIMPLEX, 0.65, _vvc[_colorset][0],1,cv::LINE_AA);
           _hrstr = "HR: " + std::to_string(_hr.second) + " bpm";
           cv::putText(frame, _hrstr, cv::Point(4,45), cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(0,0,0),1,cv::LINE_AA);
           cv::putText(frame, _hrstr, cv::Point(3,44), cv::FONT_HERSHEY_SIMPLEX, 0.65, _vvc[_colorset][1],1,cv::LINE_AA);
           drawDataWindow("VPG (normalized)", cv::Size(640,240), _vpgsignals, pulseprocfirst.getLength(), 3.0,-3.0,_vvc[_colorset]);
           //drawDataWindow("HRV", cv::Size(640,240), _vhrvsignals, peakdetfirst.getIntervalsLength(), 1100.0,300.0,_vcolors);

           cv::rectangle(faceregion,_selectionpair.first,_vvc[_colorset][0],1);
           cv::rectangle(faceregion,_selectionpair.second,_vvc[_colorset][1],1);
           cv::imshow("Select regions",faceregion);

           cv::RotatedRect _faceRrect = facetracker.getFaceRotatedRect();
           /*cv::Point2f _vert[4]; // Rectangle around the face
           _faceRrect.points(_vert);
           for(unsigned char i = 0; i < 4; ++i) {
               cv::line(frame,_vert[i], _vert[(i+1)%4],cv::Scalar(0,0,255),1,cv::LINE_AA);
           }*/
           if(facetracker.getFaceAlignMethod() == FaceTracker::FaceShapeDlib ||
              facetracker.getFaceAlignMethod() == FaceTracker::FaceShapeOpencv) {
               ofs << pulseprocfirst.getSignalSampleValue() << ",\t" << pulseprocsecond.getSignalSampleValue();
               dlib::full_object_detection _faceshape = facetracker.getFaceShape();
               for(size_t i = 0; i < _faceshape.num_parts(); ++i) {
                   dlib::point _p = _faceshape.part(i), _tp;
                   float _anglerad = - CV_PI * _faceRrect.angle / 180.0;
                   _tp.x() = _p.x()*std::cos(_anglerad) + _p.y()*std::sin(_anglerad);
                   _tp.y() = - _p.x()*std::sin(_anglerad) + _p.y()*std::cos(_anglerad);
                   _tp += dlib::point(_faceRrect.center.x,_faceRrect.center.y);
                   _faceshape.part(i) = _tp;
                   ofs << ",\t" << _tp.x() << ",\t" << _tp.y();
               }
               ofs << std::endl;
               render_face_shape(frame,_faceshape);
            }
        }

        cv::String _periodstr = num2str(frame.cols,0) + "x" + num2str(frame.rows,0) + " " + num2str(t) + " ms";
        cv::putText(frame, _periodstr, cv::Point(4,frame.rows-20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,0), 1, cv::LINE_AA);
        cv::putText(frame, _periodstr, cv::Point(3,frame.rows-21), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1, cv::LINE_AA);
        static const cv::String _options = "options keys: s - video settings; esc - quit";
        cv::putText(frame, _options, cv::Point(4,frame.rows-6), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,0), 1, cv::LINE_AA);
        cv::putText(frame, _options, cv::Point(3,frame.rows-7), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1, cv::LINE_AA);
        cv::imshow(APP_NAME, frame);
        int c = cv::waitKey(1);
        if( (char)c == 27 ) { // 27 is escape ASCII code
            break;
        } else switch(c) {
            case 's':
                videocapture.set(cv::CAP_PROP_SETTINGS,0.0);
                break;
            case 'r':
                _colorset = 0;
                break;
            case 'g':
                _colorset = 1;
                break;
            case 'b':
                _colorset = 2;
                break;
            case 'o':
                facetracker.setFaceAlignMethod(FaceTracker::FaceShapeOpencv);
                break;
            case 'd':
                facetracker.setFaceAlignMethod(FaceTracker::FaceShapeDlib);
                break;
        }
    }

    videocapture.release();
    return 0;
}

template <typename T>
std::string num2str(T value, unsigned char precision)
{
    std::string _fullstring = std::to_string(value);
    size_t _n = 0;
    for(size_t i = 0; i < _fullstring.size(); ++i) {        
        if(_fullstring[i] == '.')
            break;
                _n++;
    }
    if(precision > 0) {
        _n += precision + 1;
    }
    return std::string(_fullstring.begin(), _fullstring.begin() + _n);
}

// Grabbed from http://www.learnopencv.com/speeding-up-dlib-facial-landmark-detector/
void draw_polyline(cv::Mat &img, const dlib::full_object_detection& d, const int start, const int end, bool isClosed = false)
{
    std::vector <cv::Point> points;
    for (int i = start; i <= end; ++i) {
        //cv::putText(img,std::to_string(i),cv::Point(d.part(i).x(), d.part(i).y()),cv::FONT_HERSHEY_SIMPLEX,0.4,cv::Scalar(0,0,255),1,cv::LINE_AA);
        points.push_back(cv::Point(d.part(i).x(), d.part(i).y()));
    }
    cv::polylines(img, points, isClosed, cv::Scalar(0,255,0), 1, cv::LINE_AA);
}

void render_face_shape (cv::Mat &img, const dlib::full_object_detection& d)
{   
    if(d.num_parts() > 0) {
        if(d.num_parts() == 68) {
            draw_polyline(img, d, 0, 16);           // Jaw line
            draw_polyline(img, d, 17, 21);          // Left eyebrow
            draw_polyline(img, d, 22, 26);          // Right eyebrow
            draw_polyline(img, d, 27, 30);          // Nose bridge
            draw_polyline(img, d, 30, 35, true);    // Lower nose
            draw_polyline(img, d, 36, 41, true);    // Left eye
            draw_polyline(img, d, 42, 47, true);    // Right Eye
            draw_polyline(img, d, 48, 59, true);    // Outer lip
            draw_polyline(img, d, 60, 67, true);    // Inner lip
        } else if(d.num_parts() == 5) {
            draw_polyline(img, d, 0, 1);    // Left eye
            draw_polyline(img, d, 2, 3);    // Right Eye
            draw_polyline(img, d, 4, 4);    // Lower nose
        }
    }
}

void drawDataWindow(const cv::String &_title, const cv::Size _windowsize, const std::vector<const float *> _data, const int _datalength, float _ymax, float _ymin, const std::vector<cv::Scalar> &_colors)
{
    if((_datalength > 0) && (_windowsize.area() > 0) && (_data.size() > 0)  && (_data.size() == _colors.size())) {

        const cv::Scalar _backgroundcolor   = cv::Scalar(15,15,15);
        const cv::Scalar _coordcolor        = cv::Scalar(55,55,55);
        const cv::Scalar _fontcolor         = cv::Scalar(155,155,155);

        cv::Mat _colorplot = cv::Mat::zeros(_windowsize, CV_8UC3);
        cv::rectangle(_colorplot,cv::Rect(0,0,_colorplot.cols,_colorplot.rows),_backgroundcolor, -1);

        int _ticksX = 10;
        float _tickstepX = static_cast<float>(_windowsize.width)/ _ticksX ;
        for(int i = 1; i < _ticksX ; i++)
            cv::line(_colorplot, cv::Point2f(i*_tickstepX,0), cv::Point2f(i*_tickstepX,_colorplot.rows), _coordcolor, 1);

        int _ticksY = 8;
        float _tickstepY = static_cast<float>(_windowsize.height)/ _ticksY ;
        for(int i = 1; i < _ticksY ; i++) {
            cv::line(_colorplot, cv::Point2f(0,i*_tickstepY), cv::Point2f(_colorplot.cols,i*_tickstepY), _coordcolor, 1);
            cv::putText(_colorplot, num2str(_ymax - i * (_ymax-_ymin)/_ticksY), cv::Point(5, i*_tickstepY - 10), cv::FONT_HERSHEY_SIMPLEX, 0.4, _fontcolor, 1, cv::LINE_AA);
        }

        float invstepY = (_ymax - _ymin) / _windowsize.height;
        float stepX = static_cast<float>(_windowsize.width) / (_datalength - 1);

        for(size_t c = 0; c < _data.size(); ++c) {
            const float *_curve = _data[c];
            for(int i = 0; i < _datalength - 1; i++) {
                cv::line(_colorplot, cv::Point2f(i*stepX, _windowsize.height - (_curve[i] - _ymin)/invstepY),
                         cv::Point2f((i+1)*stepX, _windowsize.height - (_curve[i+1] - _ymin)/invstepY),
                        _colors[c], 1, cv::LINE_AA);
            }
        }

        cv::imshow(_title, _colorplot);
    }
}

void loadSelection(void* userdata)
{
    cv::FileStorage _cvfilestorage;
    if(_cvfilestorage.open(cv::String(APP_NAME) + cv::String(".yml"), cv::FileStorage::READ)) {
        std::pair<cv::Rect,cv::Rect> *_selectionrects = reinterpret_cast<std::pair<cv::Rect,cv::Rect>*>(userdata);
        _cvfilestorage["rect_1"] >> _selectionrects->first;
        _cvfilestorage["rect_2"] >> _selectionrects->second;
    }
    _cvfilestorage.release();
}

void saveSelection(void* userdata)
{
    cv::FileStorage _cvfilestorage(cv::String(APP_NAME) + cv::String(".yml"), cv::FileStorage::WRITE);
    std::pair<cv::Rect,cv::Rect> *_selectionrects = reinterpret_cast<std::pair<cv::Rect,cv::Rect>*>(userdata);
    _cvfilestorage << "rect_1" << _selectionrects->first;
    _cvfilestorage << "rect_2" << _selectionrects->second;
    _cvfilestorage.release();
}

void selectRegionByMouse(int event, int x, int y, int flags, void* userdata)
{
    static int leftbtnX0 = 0, leftbtnY0 = 0, leftbtnPressed = 0,
               rightbtnX0 = 0, rightbtnY0 = 0, rightbtnPressed = 0;
    switch(event) {
        case cv::EVENT_LBUTTONDOWN:
            leftbtnX0 = x;
            leftbtnY0 = y;
            leftbtnPressed = 1;
            break;
        case cv::EVENT_LBUTTONUP:
            leftbtnPressed = 0;
            saveSelection(userdata);
            break;
        case cv::EVENT_MOUSEMOVE:
            if(leftbtnPressed) {
                cv::Rect &_selection = reinterpret_cast<std::pair<cv::Rect,cv::Rect>*>(userdata)->first;
                if(x < leftbtnX0) {
                    _selection.x = x;
                    _selection.width = leftbtnX0-x;
                } else {
                    _selection.x = leftbtnX0;
                    _selection.width = x-leftbtnX0;
                }
                if(y < leftbtnY0) {
                    _selection.y = y;
                    _selection.height = leftbtnY0-y;
                } else {
                    _selection.y = leftbtnY0;
                    _selection.height = y-leftbtnY0;
                }
            }
            if(rightbtnPressed) {
                cv::Rect &_selection = reinterpret_cast<std::pair<cv::Rect,cv::Rect>*>(userdata)->second;
                if(x < rightbtnX0) {
                    _selection.x = x;
                    _selection.width = rightbtnX0-x;
                } else {
                    _selection.x = rightbtnX0;
                    _selection.width = x-rightbtnX0;
                }
                if(y < rightbtnY0) {
                    _selection.y = y;
                    _selection.height = rightbtnY0-y;
                } else {
                    _selection.y = rightbtnY0;
                    _selection.height = y-rightbtnY0;
                }
            }            
            break;
    case cv::EVENT_RBUTTONDOWN:
        rightbtnX0 = x;
        rightbtnY0 = y;
        rightbtnPressed= 1;
        break;
    case cv::EVENT_RBUTTONUP:
        rightbtnPressed= 0;
        saveSelection(userdata);
        break;
    }
}
