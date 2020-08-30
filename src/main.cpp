#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

constexpr double FORWARD_BUFFER = 30;
constexpr double BACKWARD_BUFFER = 10;

bool CheckPrediction(json sensor_fusion, int lane, double car_s, int window, bool check_back)
{
  for (int i = 0; i < sensor_fusion.size(); ++i)
  {
    auto d = sensor_fusion[i][6];
    if (d < (2 + 4 * lane + 2) && d > (2 + lane * 4 - 2))
    {
      double vx = sensor_fusion[i][3];
      double vy = sensor_fusion[i][4];
      auto check_speed = sqrt(vx * vx + vy * vy);
      double check_car_s = sensor_fusion[i][5];

      check_car_s += window * 0.02 * check_speed;

      if ((check_car_s > car_s) && (check_car_s - car_s < FORWARD_BUFFER))
      {
        return true;
      }
      else if (check_back && (check_car_s < car_s) && (car_s - check_car_s < BACKWARD_BUFFER))
      {
        return true;
      }
    }
  }
  return false;
}

void ComputeState(int prev_size, double end_path_s, double car_s, json sensor_fusion, int &lane, double &ref_vel)
{
  if (prev_size > 2)
  {
    car_s = end_path_s;
  }
  bool too_close = false;

  too_close = CheckPrediction(sensor_fusion, lane, car_s, prev_size, false);

  if (too_close)
  {
    int left_lane = lane - 1;
    int right_lane = lane + 1;
    if (left_lane >= 0)
    {
      bool too_close_left = CheckPrediction(sensor_fusion, left_lane, car_s, prev_size, true);
      if (too_close_left)
      {
        if (right_lane <= 2)
        {
          bool too_close_right = CheckPrediction(sensor_fusion, right_lane, car_s, prev_size, true);
          if (too_close_right)
          {
            // keep lane
          }
          else
          {
            lane = right_lane;
          }
        }
        else
        {
          bool too_close_left1 = CheckPrediction(sensor_fusion, left_lane, car_s, prev_size, true);
          if (too_close_left1)
          {
            //keep lane
          }
          else
          {
            lane = left_lane;
          }
        }
      }
      else
      {
        lane = left_lane;
      }
    }
    else
    {
      bool too_close_right = CheckPrediction(sensor_fusion, right_lane, car_s, prev_size, true);
      if (too_close_right)
      {
        // keep lane
      }
      else
      {
        lane = right_lane;
      }
    }

    ref_vel -= .224;
  }

  else if (ref_vel < 49.5)
  {
    ref_vel += .224;
  }
}
int main()
{
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line))
  {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  int lane = 1;
  double ref_vel = 0;

  h.onMessage([&map_waypoints_x, &map_waypoints_y, &map_waypoints_s,
               &map_waypoints_dx, &map_waypoints_dy, &lane, &ref_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                                                                      uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2')
    {

      auto s = hasData(data);

      if (s != "")
      {
        auto j = json::parse(s);

        string event = j[0].get<string>();

        if (event == "telemetry")
        {
          // j[1] is the data JSON object

          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          vector<double> next_x_vals;
          vector<double> next_y_vals;
          int prev_size = previous_path_x.size();
          if (prev_size > 2)
          {
            car_s = end_path_s;
          }
          bool too_close = false;

          too_close = CheckPrediction(sensor_fusion, lane, car_s, prev_size, false);

          if (too_close)
          {
            int left_lane = lane - 1;
            int right_lane = lane + 1;

            // // always prefer middle lane if safe
            // if (!CheckPrediction(sensor_fusion, 1, car_s, prev_size, true))
            // {
            //   lane = 1;
            // }
            // if there is a lane to the left of us
            if (left_lane >= 0 && !CheckPrediction(sensor_fusion, left_lane, car_s, prev_size, true))
            {
              // if changing lane to the left safe is safe
              lane = left_lane;
            }
            else if (right_lane <= 2 && !CheckPrediction(sensor_fusion, right_lane, car_s, prev_size, true))
            {
              lane = right_lane;
            }
            else
            {
            }
            ref_vel -= .224;
          }
          else if (ref_vel < 49.5)
          {
            ref_vel += .224;
          }

          // ComputeState(prev_size, end_path_s, car_s, sensor_fusion, lane, ref_vel);

          vector<double> ptsx;
          vector<double> ptsy;
          double ref_x = car_x;
          double ref_y = car_y;
          double ref_yaw = deg2rad(car_yaw);

          if (prev_size < 2)
          {
            double prev_car_x = car_x - cos(car_yaw);
            double prev_car_y = car_y - sin(car_yaw);

            ptsx.push_back(prev_car_x);
            ptsy.push_back(prev_car_y);

            ptsx.push_back(car_x);
            ptsy.push_back(car_y);
          }
          else
          {
            double ref_x_prev = previous_path_x[prev_size - 2];
            double ref_y_prev = previous_path_y[prev_size - 2];

            ptsx.push_back(ref_x_prev);
            ptsy.push_back(ref_y_prev);

            ref_x = previous_path_x[prev_size - 1];
            ref_y = previous_path_y[prev_size - 1];

            ptsx.push_back(ref_x);
            ptsy.push_back(ref_y);

            ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);
          }

          //calculate the rest of 3 points
          vector<double> XY3 = getXY(car_s + 30, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> XY4 = getXY(car_s + 60, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> XY5 = getXY(car_s + 90, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

          ptsx.push_back(XY3[0]);
          ptsy.push_back(XY3[1]);

          ptsx.push_back(XY4[0]);
          ptsy.push_back(XY4[1]);

          ptsx.push_back(XY5[0]);
          ptsy.push_back(XY5[1]);

          for (int i = 0; i < ptsx.size(); ++i)
          {
            double shift_x = ptsx[i] - ref_x;
            double shift_y = ptsy[i] - ref_y;

            ptsx[i] = shift_x * cos(-ref_yaw) - shift_y * sin(-ref_yaw);
            ptsy[i] = shift_x * sin(-ref_yaw) + shift_y * cos(-ref_yaw);
          }

          tk::spline s;
          s.set_points(ptsx, ptsy);

          for (int i = 0; i < previous_path_x.size(); ++i)
          {
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }

          double target_x = 30.0;
          double target_y = s(target_x);
          double target_dist = sqrt((target_x * target_x) + (target_y * target_y));

          double x_add_on = 0;
          for (int i = 1; i <= 50 - previous_path_x.size(); i++)
          {
            double N = target_dist / (0.02 * ref_vel / 2.24);
            double x_point = x_add_on + target_x / N;
            double y_point = s(x_point);
            x_add_on = x_point;
            double x_ref = x_point;
            double y_ref = y_point;

            x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
            y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));
            x_point += ref_x;
            y_point += ref_y;
            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);
          }

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\"," + msgJson.dump() + "]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        } // end "telemetry" if
      }
      else
      {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    } // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port))
  {
    std::cout << "Listening to port " << port << std::endl;
  }
  else
  {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }

  h.run();
}