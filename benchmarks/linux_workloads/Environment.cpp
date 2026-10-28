#include "../common/Adapter.hpp"
#include "Platform.hpp"
namespace bench {
std::string quote(std::string_view value){return linuxbench::quote(value);}
std::string environment(std::string_view name){
    auto selected=linuxwork::selected(name);auto result=linuxbench::environment();result.pop_back();
    result+=",\"gpu\":"+quote(name)+",\"vendor_id\":"+std::to_string(selected.vendor)+",\"device_id\":"+std::to_string(selected.device)
        +",\"software_renderer\":"+(selected.type==4?"true":"false")+",\"driver_query_available\":true,\"driver_uuid\":"+quote(selected.driverUuid)
        +",\"device_uuid\":"+quote(selected.uuid)+",\"driver_version_raw\":"+std::to_string(selected.driver)+",\"native_device_type\":"+std::to_string(selected.type)+",\"device_luid\":null}";
    return result;
}
}
