// The view-culling test may call a box outside only when none of its points lies inside the clip volume.
#include "../../deep/engine/clip_box.h"
static void clip_box_checks(const auto& require){
    uint32_t seed=1357924680u;auto random=[&]{seed=seed*1664525u+1013904223u;return float(double(seed>>8)/double(1<<24));};
    auto between=[&](float a,float b){return a+(b-a)*random();};
    const float identity[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float low3[3]{-.5f,-.5f,.2f},high3[3]{.5f,.5f,.3f};
    require(!ClipBox::outside(low3,high3,identity),"Clip box: a box inside the clip volume is visible");
    const float side[3]{5,0,0},sideHigh[3]{6,1,1};
    require(ClipBox::outside(side,sideHigh,identity),"Clip box: a box beyond the right plane is outside");
    const float behind[3]{0,0,2},behindHigh[3]{1,1,3};
    require(ClipBox::outside(behind,behindHigh,identity),"Clip box: a box beyond the far plane is outside");
    const float before[3]{0,0,-3},beforeHigh[3]{1,1,-2};
    require(ClipBox::outside(before,beforeHigh,identity),"Clip box: a box in front of the near plane is outside");
    const float straddle[3]{.5f,0,.2f},straddleHigh[3]{2,.5f,.3f};
    require(!ClipBox::outside(straddle,straddleHigh,identity),"Clip box: a box that crosses a plane is visible");
    // Random matrices with any sign of w, random boxes, and a dense sample of points in each box as the ground truth.
    bool conservative=true;unsigned outsideCount=0,visibleCount=0;
    for(int round=0;round<3000;++round){
        float m[16];for(float& value:m)value=between(-2,2);
        float low[3],high[3];for(int k=0;k<3;++k){low[k]=between(-4,4);high[k]=low[k]+between(0,3);}
        const bool outside=ClipBox::outside(low,high,m);
        outside?++outsideCount:++visibleCount;
        for(int sample=0;sample<96;++sample){
            double p[3];for(int k=0;k<3;++k){p[k]=sample<8?((sample>>k)&1?high[k]:low[k]):low[k]+(high[k]-low[k])*random();}
            double c[4];for(int j=0;j<4;++j)c[j]=p[0]*m[j]+p[1]*m[4+j]+p[2]*m[8+j]+m[12+j];
            const double w=c[3];
            const bool inside=c[0]>=-w&&c[0]<=w&&c[1]>=-w&&c[1]<=w&&c[2]>=0&&c[2]<=w;
            if(inside&&outside)conservative=false;
        }
    }
    require(conservative,"Clip box: no box reported outside has a point inside the clip volume");
    require(outsideCount>200&&visibleCount>200,"Clip box: the random cases cover both outcomes");
}
