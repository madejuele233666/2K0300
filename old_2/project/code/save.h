#ifndef SAVE_H_
#define SAVE_H_

#include <signal.h>
#include <fstream>
#include <map>
#include <sstream>
#include <optional>
#include <set>
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <algorithm>

class ParamManager
{
private:
    const std::string PARAMS_FILE = "settings.txt";     //设置参数保存文件路径
    std::map<std::string, std::string> params;          //创建一个map，变量名为params，用于存新的数据
        const std::map<std::string, std::string> defaultParams = 
        {//在这里设置默认参数名及当参数名缺失时自动填充的参数值
        /***其中的变量名需要改一下****/
        {"duoji_d", "29.45"},
        {"zhidao_P", "7.70"},
        {"circle_P", "7.45"},
        {"BLDC_PWM", "850"},
        {"car_speed", "500"},
        {"zhidao_speed","500"},
        {"circle_speed","500"}
        };
        std::set<std::string> validParams;              //创建一个set，变量名为validParams，用于存string类型的数据

        void trim(std::string &str);

public:
    ParamManager();
    void load();
    void save();
    std::vector<std::string> getParamNames() const;
    std::optional<std::string> getParam(const std::string& key) const;
    bool setParam(const std::string& key, const std::string& value);
    void showAndSetParam(const std::string& key, const std::string& newValue);
    void printAllParams() const;
};


#endif /* SAVE_H_ */



