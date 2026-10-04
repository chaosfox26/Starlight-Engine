#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace starlight::editor {
// Runtime-only moisture: pause freezes it; reset restores the authored dry scene.
class WeatherSurface {
    double moisture_=0;
  public:
    void reset(){moisture_=0;}
    double moisture()const{return moisture_;}
    void step(double rain,double dt){
        if(!std::isfinite(rain)||rain<0||rain>1||!std::isfinite(dt)||dt<0||dt>.05)
            throw std::runtime_error("Invalid surface weather step");
        // Analytic integration gives the same result regardless of frame subdivision.
        double wet=.32*rain,dry=.012*(1-rain),rate=wet+dry;
        if(rate>0)moisture_=std::clamp(wet/rate+(moisture_-wet/rate)*std::exp(-rate*dt),0.,1.);
    }
};
}
