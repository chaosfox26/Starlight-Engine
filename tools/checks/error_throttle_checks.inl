// The editor's error reporting throttle (audit ED-06): a repeating failure must not write a diagnostic and open a dialog on every tick.
#include "error_throttle.hpp"
static void error_throttle_checks(const auto& require){
    using clock=std::chrono::steady_clock;
    using namespace std::chrono_literals;
    const auto start=clock::now();
    ErrorThrottle throttle(5s);
    require(throttle.admit("renderer failed",start),"ED-06: The first report of an error is always admitted");
    size_t admitted=0;
    for(int tick=1;tick<=300;++tick)if(throttle.admit("renderer failed",start+std::chrono::milliseconds(16*tick)))++admitted;
    require(admitted==0&&throttle.suppressed()==300,"ED-06: 300 identical errors at timer rate inside the window are counted, not reported again");
    require(throttle.admit("a different error",start+5s),"ED-06: A different error text is admitted at once");
    require(throttle.admit("renderer failed",start+5s+20ms),"ED-06: The earlier text is admitted again after another error in between");
    require(!throttle.admit("renderer failed",start+5s+40ms)&&throttle.admit("renderer failed",start+11s),"ED-06: The same text is reported again once the window has passed");
    ErrorThrottle fresh(5s);
    require(fresh.admit("",start)&&!fresh.admit("",start+1s),"ED-06: An empty message is throttled like any other text");
    ErrorThrottle retry(5s);
    require(retry.admit("File is busy",start,false)&&retry.admit("File is busy",start+1s,false)&&retry.suppressed()==0,"ED-06: A user retry of an action that fails again within the window is reported again");
    require(retry.admit("File is busy",start+2s,true)&&!retry.admit("File is busy",start+2s+16ms,true),"ED-06: A failure raised by the timer is still throttled");
}
