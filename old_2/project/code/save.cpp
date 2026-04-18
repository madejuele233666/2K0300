#include "save.h"


void ParamManager::trim(std::string &str)           //从给定的字符串中移除所有的空格、回车符 (\r) 和换行符 (\n)
{
    str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
    str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());
    str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
}

/**
 * 函数作用：初始化参数格式，历遍defaultParams中的所有key
 * 用来确定哪些数据是合法的
 */
ParamManager::ParamManager()                        //无参构造函数
{
    for (const auto& [key, _] : defaultParams) {
        validParams.insert(key);
    }
}

/**
 * 函数作用：从一个预定义的文件 (PARAMS_FILE) 中加载参数，
 * 并将其存储在 ParamManager 对象的 params 成员中。
 * 它还处理参数的验证、缺失参数的补充以及必要时更新参数文件
 * 
 */
void ParamManager::load() 
{
    params.clear();//加载新数据前先清空原数据
    bool needUpdate = false;//用于标记是否需要将更新后的参数写回文件
    
    std::ifstream file(PARAMS_FILE);//尝试打开PARAMS_FILE
    if (file.is_open()) 
    {
        std::string line;//逐行读取文件内容
        while (getline(file, line)) 
        {
            if (line.empty() || line[0] == '#') continue;//跳过空行和注释行
            
            size_t pos = line.find('=');
            if (pos != std::string::npos) 
            {
                //key和value的确定
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                trim(key);
                trim(value);
                //有效数据key非空且在合法数据格式中
                if (!key.empty() && validParams.count(key)) 
                {
                    params[key] = value;
                } 
                //无效数据key非空但不合法
                else if (!key.empty()) 
                {
                    std::cout << "发现无效参数: " << key << "=" << value << std::endl;
                    needUpdate = true;
                }
            }
        }
        file.close();
    }
    //检查缺失
    for (const auto& [key, defaultValue] : defaultParams) 
    {
        if (!params.count(key)) 
        {
            params[key] = defaultValue;
            needUpdate = true;
            std::cout << "添加缺失参数: " << key << "=" << defaultValue << std::endl;
        }
    }
    //文件内容更新
    if (needUpdate) 
    {
        std::cout << "参数验证: " << std::endl;
        for (const auto& [k,v] : params) 
        {
            std::cout << k << "=" << v << " "<< std::endl;
        }
        std::cout << std::endl;
        save();
    }
}

/**
 * 函数作用：保存数据
 */
void ParamManager::save() 
{
    std::string tempFile = PARAMS_FILE + ".tmp";
    std::ofstream file(tempFile);
    if (file.is_open()) 
    {
        file << "# 参数配置文件\n";
        file << "# 格式: 参数名=参数值\n\n";
        
        std::map<std::string, std::string> sortedParams(params.begin(), params.end());
        for (const auto& [key, value] : sortedParams) 
        {
            if (value.empty()) continue;
            file << key << "=" << value << "\n";
        }
        file.close();
        
        if (rename(tempFile.c_str(), PARAMS_FILE.c_str()) != 0) 
        {
            std::cerr << "保存参数文件失败: " << strerror(errno) << std::endl;
        }
    } 
    else 
    {
        std::cerr << "无法创建临时参数文件" << std::endl;
    }
}

/**
 * 函数作用:获取并返回 ParamManager 对象中所有参数的名称（键）的列表
 * 
 */
std::vector<std::string> ParamManager::getParamNames() const 
{
    std::vector<std::string> names;
    for (const auto& [key, _] : params) 
    {
        names.push_back(key);
    }
    return names;
}

/**
 * 函数作用：根据给定的键（key），从 ParamManager 对象中查找对应的参数值，并以 std::optional<string> 的形式返回。
 * 如果找不到该键，则返回一个空的 std::optional 对象
 * 
 */
std::optional<std::string> ParamManager::getParam(const std::string& key) const 
{
    if (params.count(key)) 
    {
        return params.at(key);
    }
    return std::nullopt;
}

/**
 * 函数作用：设置 ParamManager 对象中指定键 (key) 对应的参数值 (value)
 * 
 */
bool ParamManager::setParam(const std::string& key, const std::string& value) 
{
    if (!validParams.count(key)) 
    {
        std::cerr << "错误: 参数 " << key << " 不是有效参数" << std::endl;
        std::cerr << "有效参数列表: ";
        for (const auto& param : validParams) 
        {
            std::cerr << param << " ";
        }
        std::cerr << std::endl;
        return false;
    }
    
    try 
    {
        double numValue = stod(value);// 将value转换为double
        numValue = round(numValue * 1000) / 1000;// 四舍五入到一位小数
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) << numValue;// 格式化为字符串，总共1位小数
        std::string formattedValue = ss.str();
        // 特殊处理，确保小数点后只有一位
        size_t dotPos = formattedValue.find('.');
        if (dotPos != std::string::npos && formattedValue.size() > dotPos + 4) 
        {
            formattedValue = formattedValue.substr(0, dotPos + 4);
        }
        
        params[key] = formattedValue;// 将格式化后的数值字符串存储
    } 
    catch (...) 
    {
        // 捕获所有类型的异常
        params[key] = value;// 如果转换失败，则直接存储原始字符串
    }
    return true;
}

/**
 * 函数作用：以交互式的方式，首先展示指定参数的当前值（如果已设置），然后尝试将该参数设置为一个新的值，并最终显示设置成功后的新值
 * 1、正在做什么  2、当前状态是什么  3、进行修改  4、操作后最终状态
 */
void ParamManager::showAndSetParam(const std::string& key, const std::string& newValue) 
{
    std::cout << "修改参数 " << key << ":" << std::endl;
    if (auto current = getParam(key)) 
    {
        std::cout << "当前值: " << *current << std::endl;
    } 
    else 
    {
        std::cout << "当前值: 未设置" << std::endl;
    }
    
    if (setParam(key, newValue)) 
    {
        std::cout << "新值: " << *getParam(key) << std::endl << std::endl;
    }
}

/**
 * 函数作用：展示所有参数
 * 
 */
void ParamManager::printAllParams() const 
{
    std::cout << "当前参数列表:" << std::endl;
    for (const auto& [key, value] : params) 
    {
        std::cout << key << ": " << value << std::endl;
    }
    std::cout << std::endl;
}
