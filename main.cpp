#include <iostream>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <math.h>
#include <iomanip>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>

/*TODO:
communicate with the roborio
Do output from pins
figure out how to friggin calibrate it if it's on the board
control the LED ring from the board and use it to shoot
control LED strips to indicate if the shooter is lined up
detect if it is on the embedded platform, and only draw to the screen if it ISN'T
Make a better gui for calibrating, so you don't have to comment and uncomment and hardcode crap
Maybe send camera index as an argument
make second camera recognize balls
*/

using namespace cv;
using namespace std;

Rect findBiggestBlob(Mat & matImage);//this function finds the bounding rectangle of the largest contiguous region in the image

const double PI = 3.141592653589793238462643383279;
const double hor_deg = 0.07914;//in DEGREES PER PIXEL. Horizontal field of view is 50.6496 degrees
const double vert_deg = 0.08189;//in DEGREES PER PIXEL. Vertical field of view is 39.3072 degrees

int main(){
	system("fswebcam -d /dev/video0 -c cam.cfg -r 640x480");//configure camera. It runs every time because I don't know how persistant the changes are
	VideoCapture camera(0);//initialize camera

	//namedWindow("ctrl",WINDOW_AUTOSIZE);//control window for calibrating the camera - commented out unless needed for calibration

	Mat screen_cap;//camera image
	Mat hsv_img;//camera image converted to hsv
	Mat thresholded;//camera image thresholded

	//high and low hsv threshold settings, to be changed when calibrating
	int Hlow = 54;
	int Hhigh = 117;
	int Slow = 49;
	int Shigh = 255;
	int Vlow = 236;
	int Vhigh = 255;

	//track bars for each threshold variable
	createTrackbar("HL","ctrl",&Hlow,179);
	createTrackbar("HH","ctrl",&Hhigh,179);
	createTrackbar("SL","ctrl",&Slow,255);
	createTrackbar("SH","ctrl",&Shigh,255);
	createTrackbar("VL","ctrl",&Vlow,255);
	createTrackbar("VH","ctrl",&Vhigh,255);

	Rect friggin_box;//bounding rectangle for the largest blob
	//center point of the friggin box
	int center_x;
	int center_y;

	double goal_height = 66.3;//preset height to top of goal, in INCHES. For the competition goals it should be 95
	double camera_height = 48.6;//preset height of the camera, in INCHES. I don't know where we're mounting the camera on the robot yet
	double camera_angle = 14;//angle of deviance from the horizontal, in DEGREES. Should be 42 officially
	double length = 20;//width of the goal in INCHES

	double distance;//distance camera is from goal, to be calculated
	double phi;//vertical angle of deviance the sightline of the camera has from the bottom of the goal
	double theta;//horizontal angle of deviance the sightline of the camera has from the center of the goal

	char buff[1];
	buff[0] = 57;
	int pin_setup;
	pin_setup = open("/sys/class/gpio/export",O_WRONLY);
	write(pin_setup,buff,1);
	close(pin_setup);

	char bufff[3] = {'o','u','t'};
	int pin_direction;
	pin_direction = open("/sys/class/gpio/gpio57/direction",O_WRONLY);
	write(pin_direction,bufff,3);
	close(pin_direction);

	char flag[1];
	int ring_pin;
	ring_pin = open("/sys/class/gpio/gpio57/value",O_WRONLY);

	while (true){
		camera.read(screen_cap);//get the current frame

		//smooth everything out (I'm not sure if this is necessary but I'm not getting rid of it now, it works pretty well)
		erode(screen_cap,screen_cap,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		dilate(screen_cap,screen_cap,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		dilate(screen_cap,screen_cap,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		erode(screen_cap,screen_cap,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));

		cvtColor(screen_cap,hsv_img,CV_BGR2HSV);//convert the frame to HSV, because it's easier to threshold (theoretically)

		inRange(hsv_img,Scalar(Hlow,Slow,Vlow),Scalar(Hhigh,Shigh,Vhigh),thresholded);//threshold it

		//blur it again (to get rid of noise)
		erode(thresholded,thresholded,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		dilate(thresholded,thresholded,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		dilate(thresholded,thresholded,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));
		erode(thresholded,thresholded,getStructuringElement(MORPH_ELLIPSE,Size(5,5)));

		friggin_box = findBiggestBlob(thresholded);//get the bounding box of the biggest goal
		rectangle(screen_cap,friggin_box,Scalar(0,0,255));//draw it onto the screen because I want to

		//show the image and the thresholded image, only necessary for calibration
		//imshow("thresholded",thresholded);
		//imshow("screen_cap",screen_cap);

		//calculate the center of the box, the distance from the goal, and the angle of deviance of the sightline
		center_x = friggin_box.x + (friggin_box.width/2.0);
		center_y = friggin_box.y + (friggin_box.height/2.0);
		phi = (240 - friggin_box.y)*vert_deg;
		distance = (goal_height-camera_height)/tan((phi+camera_angle)*(PI/180.0));//distance from the nearest point of the goal
		distance += sqrt(pow(length/2.0,2) - pow(distance*tan((friggin_box.width/2.0)*hor_deg*(PI/180.0)),2));//account for angle of approach
		theta = (center_x-320)*hor_deg;//in DEGREES. Positive value of theta indicates the robot must turn in the COUNTERCLOCKWISE direction because that's how math works

		cout << "DISTANCE:" << setw(9) << distance << "   ANGLE:" << setw(9) << theta << '\n';

		//exit program if pressing ESC
		if (waitKey(10) == 27)
			return 0;

		write(ring_pin,flag,1);
		if (flag[0] == 1)
			flag[0] = 0;
		else if (flag[0] == 0)
			flag[0] = 1;
		cout << flag[0] << '\n';
		system("sleep 1");
	}

	return 0;
}


Rect findBiggestBlob(Mat &matImage){
	int largest_area = 0;
	int largest_contour_index = 0;
	Rect bounding_rect;
	Mat img_clone = matImage.clone();

	vector<vector<Point> > contours;
	vector<Vec4i> hierarchy;

	findContours(img_clone, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE );

	for(int i = 0; i < contours.size(); i++){
		double a = contourArea(contours[i], false);
		if (a > largest_area){
			largest_area = a;
			largest_contour_index = i;
			bounding_rect = boundingRect(contours[i]);
		}
	}

	return bounding_rect;
}
