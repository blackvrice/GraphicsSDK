#include "../common/LinuxEnvironment.hpp"
#include <iostream>
int main()try{
    using namespace linuxbench;Identity gpu{"GPU",std::string(32,'a'),std::string(32,'b'),1,1,2,3,VK_API_VERSION_1_1,2};
    Identity cpu=gpu;cpu.uuid=std::string(32,'c');cpu.name="CPU";cpu.type=4;unsigned rejected{};
    auto reject=[&](const std::vector<Identity>& rows,const std::string& uuid,const char* mode){try{select(rows,uuid,mode);}catch(const std::runtime_error&){++rejected;return;}throw std::runtime_error("Invalid selection accepted");};
    require(select({gpu},gpu.uuid,"physical").name=="GPU","Physical model");require(select({cpu},cpu.uuid,"software-diagnostic").name=="CPU","Software model");
    reject({gpu},std::string(32,'A'),"physical");reject({gpu},cpu.uuid,"physical");reject({gpu,gpu},gpu.uuid,"physical");
    auto duplicate=gpu;duplicate.uuid=cpu.uuid;reject({gpu,duplicate},gpu.uuid,"physical");
    reject({cpu},cpu.uuid,"physical");reject({gpu},gpu.uuid,"software-diagnostic");auto virtualGpu=gpu;virtualGpu.type=3;reject({virtualGpu},gpu.uuid,"physical");
    require(rejected==7,"Selection rejection coverage");std::cout<<"{\"assertions\":9,\"rejections\":7,\"native_GPU_executed\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
