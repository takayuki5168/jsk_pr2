#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <ros/ros.h>
#include <std_msgs/Float64.h>

class ParticleSystemNode
{
public:
  explicit ParticleSystemNode() : nh_(""), pnh_("")
  {
    ref_state = {0, 0};   // FIX
    now_state = {0, 0};   // FIX 
    now_input = {0, 0};   // FIX
    past_particle = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    sub_pitch_ = pnh_.subscribe("calc_diabolo_state/pitch", 1, &ParticleSystemNode::messageCallbackForPitch, this);   // FIX
    sub_yaw_ = pnh_.subscribe("calc_diabolo_state/yaw", 1, &ParticleSystemNode::messageCallbackForYaw, this);   // FIX
    sub_idle_ = pnh_.subscribe("calc_diabolo_state/idle", 1, &ParticleSystemNode::messageCallbackForIdle, this);
    
    pub_arm_ = pnh_.advertise<std_msgs::Float64>("arm", 10);   // FIX
    pub_base_ = pnh_.advertise<std_msgs::Float64>("base", 10);   // FIX
  }

  void loop()
  {
    while (true) {
      if (whether_idle == 0) {
	continue;
      }

      ros::spinOnce();
      calc_now_input();
      publish();
      update_past_particle();
    }
  }
  
  void loadData(std::vector<std::string> log_files)
  {
    double arm, base, pitch, yaw;
    std::vector<double> arms, bases, pitchs, yaws;
    
    for (int l_idx = 0; l_idx < log_files.size(); l_idx++) {
      std::ifstream log(log_files.at(l_idx));
      int cnt = 0;
      while (not log.fail()){
	cnt++;
	log >> arm >> base >> pitch >> yaw;
	
	arms.push_back(arm);
	bases.push_back(base);
	pitchs.push_back(pitch);
	yaws.push_back(yaw);

	if (cnt < std::max(past_state_num + 1, past_input_num + 1)) continue;

	std::array<double, var_num> particle;
	int e_cnt = 0;
	for (int ps_idx = 0; ps_idx < past_state_num + 1; ps_idx++) {
	  particle.at(e_cnt) = pitchs.at(pitchs.size() - ps_idx - 1);
	  e_cnt++;
	  particle.at(e_cnt) = yaws.at(yaws.size() - ps_idx - 1);
	  e_cnt++;	  
	}
	for (int pi_idx = 0; pi_idx < past_input_num + 1; pi_idx++) {
	  particle.at(e_cnt) = arms.at(arms.size() - pi_idx - 1);
	  e_cnt++;	  
	  particle.at(e_cnt) = bases.at(bases.size() - pi_idx - 1);
	  e_cnt++;	  
	}
	particles.push_back(particle);
      }
      log.close();
      std::cout << log_files.at(l_idx) << std::endl;
    }
  }
  
  void saveData() {}
  
private:
  ros::NodeHandle nh_, pnh_;
  ros::Subscriber sub_pitch_, sub_yaw_, sub_idle_;   // FIX
  ros::Publisher pub_arm_, pub_base_;

  const static int state_dim = 2;
  const static int past_state_num = 2;
  const static int input_dim = 2;
  const static int past_input_num = 1;
  const static int var_num = state_dim * (past_state_num + 1) + input_dim * (past_input_num + 1);

  std::vector<std::array<double, var_num>> particles;
  std::array<int, var_num> past_particle;
  std::array<int, state_dim> ref_state;
  std::array<int, state_dim> now_state;
  std::array<int, input_dim> now_input;
  
  int whether_idle = 1;

  void messageCallbackForPitch(std_msgs::Float64 msg) { now_state.at(0) = msg.data; }   // FIX
  void messageCallbackForYaw(std_msgs::Float64 msg) { now_state.at(1) = msg.data; }   // FIX
  void messageCallbackForIdle(std_msgs::Float64 msg) {whether_idle = static_cast<int>(msg.data); }
  
  void calc_now_input()
  {
    std::array<double, state_dim> width_state{20, 20};   // TODO 
    std::array<double, input_dim> width_input{20, 20};   // TODO
    double min_state_diff = 1000000.;
    int min_p_idx = 0;

    for (int p_idx = 0; p_idx < particles.size(); p_idx++) {
      bool flag = true;
      for (int e_idx = 0; e_idx < var_num; e_idx++) {
	if (e_idx < state_dim) {   // x_t
	  continue;
	} else if (e_idx < state_dim * (past_state_num + 1)) {   // x_t-1 x_t-2 ...
	  double diff = std::abs(particles.at(p_idx).at(e_idx) - past_particle.at(e_idx));
	  if (diff > width_state.at(e_idx % state_dim)) flag = false;
	}else if (e_idx < state_dim * (past_state_num + 1) + input_dim) {   // u_t
	  continue;
        } else {   // u_t-1 u_t-2 ...
    	  double diff = std::abs(particles.at(p_idx).at(e_idx) - past_particle.at(e_idx));
	  if (diff > width_input.at(e_idx % input_dim)) flag = false;
	}
	
	if (not flag) break;
      }
      
      if (not flag) continue;      

      // compare distance between x_r and x_t & calc min_p_idx
      double state_diff = 0.;
      for (int s_idx = 0; s_idx < state_dim; s_idx++) {
	state_diff += std::abs(particles.at(p_idx).at(s_idx) - now_state.at(s_idx));
      }
      //std::cout << state_diff << std::endl;
      if (state_diff < min_state_diff) {
	min_state_diff = state_diff;
	min_p_idx = p_idx;
      }
    }

    // extract now input from min_p_idx of particles
    for (int i_idx = 0; i_idx < input_dim; i_idx++) {
      now_input[i_idx] = particles.at(min_p_idx).at(state_dim * (past_state_num + 1) + i_idx);
    }
    
    std::cout << "p_idx:" << min_p_idx << std::endl;
  }

  void publish()
  {
    // publish arm
    std_msgs::Float64 msg_arm;
    msg_arm.data = now_input[0];
    pub_arm_.publish(msg_arm);

    // publish base
    std_msgs::Float64 msg_base;
    msg_base.data = now_input[1];
    pub_base_.publish(msg_base);    
  }

  void update_past_particle()
  {
    // update state
    for (int ps_idx = past_state_num - 1; ps_idx > 0; ps_idx--) {
      for (int s_idx = 0; s_idx < state_dim; s_idx++) {
	past_particle.at(state_dim * ps_idx + s_idx) = past_particle.at(state_dim * ps_idx + s_idx - state_dim);
      }
    }
    for (int s_idx = 0; s_idx < state_dim; s_idx++) {      
      past_particle.at(s_idx) = now_state.at(s_idx);
    }

    // update input
    for (int pi_idx = past_input_num - 1; pi_idx > 0; pi_idx--) {
      for (int i_idx = 0; i_idx < input_dim; i_idx++) {
	past_particle.at(state_dim * (past_state_num + 1) + input_dim * pi_idx + i_idx) = past_particle.at(state_dim * (past_state_num + 1) + input_dim * pi_idx + i_idx - input_dim);
      }
    }
    for (int i_idx = 0; i_idx < input_dim; i_idx++) {      
      past_particle.at(state_dim * (past_state_num + 1) + i_idx) = now_input.at(i_idx);
    }
  }
 };

int main(int argc, char** argv) {
  ros::init(argc, argv, "particle_system");
  
  std::vector<std::string> log_files{"../log/log-by-logger/log-by-loggerpy0.log",
      "../log/log-by-logger/log-by-loggerpy1.log",
      "../log/log-by-logger/log-by-loggerpy2.log",
      "../log/log-by-logger/log-by-loggerpy3.log"};

  ParticleSystemNode psn = ParticleSystemNode();
  psn.loadData(log_files);
  psn.loop();
  
  return 0;
}
