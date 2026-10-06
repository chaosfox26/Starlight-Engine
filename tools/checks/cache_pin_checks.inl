// Pin/warm interleaving (audit safeguard S3). Scene preparation warms every object's geometry on several threads while the cache is allowed to evict
// unpinned geometry. Each object must be pinned before it is warmed; otherwise another thread's insert can evict it in between and it is rebuilt during play.
// A tiny budget forces an eviction attempt on every insert, so any unpinned geometry is at risk the whole time it is not pinned.
template<class Require> void cache_pin_checks(Require require){
    auto& cache=ObjectCache::shared();
    uint64_t lateTotal=0;size_t rounds=0,objects=0;
    for(int round=0;round<4;++round){
        cache.clear();
        SceneData scene;scene.Game.emplace();scene.Game->SpawnX=0;scene.Game->SpawnY=1.7;scene.Game->SpawnZ=12;scene.Game->Collision=true;
        for(int i=0;i<28;++i){
            Character c;c.Id="pin-"+std::to_string(round)+"-"+std::to_string(i);c.Name=c.Id;c.Shape="boulder";c.X=-12+i%7*4;c.Z=-8+i/7*4;
            c.Size=.6+i*.07+round*.013;c.Y=c.Size*.6;c.KeepAboveFloor=false;c.Dynamic=false;scene.Characters.push_back(c);
        }
        {
            AdventurePlayer player;player.start(scene);
            cache.set_geometry_limit_for_test(1);
            std::atomic<double> fraction{0};
            player.prepare_geometry(fraction);
            cache.mark_prepared();
            for(const auto& c:scene.Characters)(void)cache.warm_query_geometry(c);
            const auto stats=cache.stats();
            lateTotal+=stats.lateGeometryBuilds;objects+=scene.Characters.size();++rounds;
            cache.set_geometry_limit_for_test(0);
        }
        cache.clear();
    }
    cache.set_geometry_limit_for_test(0);
    std::cout<<"cache pin checks: "<<rounds<<" rounds, "<<objects<<" objects, "<<lateTotal<<" geometry rebuilds after preparation\n";
    require(rounds==4&&objects==4*28,"S3: Every round prepares all of its objects under a one-byte geometry budget");
    require(lateTotal==0,"S3: No object's geometry is evicted between its warm-up and its pin, so nothing is rebuilt after preparation");
}
