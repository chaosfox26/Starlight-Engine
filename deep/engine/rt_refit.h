#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// A top-level refit (PERFORM_UPDATE) keeps each instance's bottom-level geometry, so it is only valid
// when instance slot i still names the geometry key the acceleration structure was built with.
template<class Item>
inline bool same_instance_keys(const std::vector<Item>& built,const std::vector<Item>& next){
 if(built.size()!=next.size())return false;
 for(size_t i=0;i<next.size();++i)if(built[i].key!=next[i].key)return false;
 return true;
}
