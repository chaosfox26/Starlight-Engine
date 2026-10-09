// Terrain brush range handling (audit ED-21): a finite but huge position or radius used to overflow the double-to-int conversion for the cell range.
static void terrain_brush_checks(const auto& require){
    TerrainData normal;
    normal.brush(0,0,2,1,"raise",0,0,1);
    size_t raised=0;for(double h:normal.Heights)if(h!=0)++raised;
    require(raised>0&&raised<size_t(TerrainData::N)*TerrainData::N/4,"ED-21: An ordinary raise brush changes a bounded patch of the terrain");
    TerrainData distant;
    distant.brush(1e300,1e300,1,1,"raise",0,0,1);
    size_t farChanged=0;for(double h:distant.Heights)if(h!=0)++farChanged;
    require(farChanged==0,"ED-21: A brush centred astronomically far away leaves the terrain untouched");
    TerrainData huge;
    huge.brush(0,0,1e300,1,"raise",0,0,1);
    size_t hugeChanged=0;bool finite=true;for(double h:huge.Heights){if(h!=0)++hugeChanged;finite=finite&&std::isfinite(h);}
    require(finite&&hugeChanged==size_t(TerrainData::N)*TerrainData::N,"ED-21: A brush with an astronomically large radius covers the whole terrain instead of overflowing the cell range");
    TerrainData paint;
    paint.brush(0,0,1e300,1,"paint",3,0,1);
    size_t painted=0;for(int v:paint.Paint)if(v==3)++painted;
    require(painted==size_t(TerrainData::N)*TerrainData::N,"ED-21: The paint tool also handles huge values without undefined behaviour");
    {   // 07-T3: stamping a vast object raises the terrain under all of it, as a moderate one does (the stamp's cell range used the same unclamped int conversion).
        auto stamped=[](double size){Character c;c.Shape="cube";c.Size=size;TerrainData t;stamp_shape(t,c);size_t n=0;for(double h:t.Heights)if(h==20)++n;return n;};
        const size_t moderate=stamped(1e6),vast=stamped(1e10);
        require(moderate==size_t(TerrainData::N)*TerrainData::N&&vast==moderate,"07-T3: A vast stamped object raises the same terrain cells as a moderate one");
    }
}
