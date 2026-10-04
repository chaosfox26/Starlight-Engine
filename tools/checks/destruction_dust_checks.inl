template<class Require> void destruction_dust_checks(Require require,const fs::path& folder){
    DestructionDust dust;dust.burst({0,1,0},{0,1,0},{.3,.35,.4},1,71,true);
    require(dust.puffs().size()==6&&dust.grains().size()==36,"Shattering creates independent mineral clouds and grit without replacing rigid fragments");
    auto initial=dust.puffs().front();auto grain=dust.grains().front();std::vector<Bounds> none;
    for(int i=0;i<120;++i)dust.advance(1./120.,nullptr,none,{.8,0,.2});
    require(dust.puffs().front().extent()>initial.extent()&&dust.puffs().front().opacity()<initial.opacity()&&(dust.puffs().front().position-initial.position).length()>.1,"Dust expands, drifts with wind and loses optical density over time");
    require(dust.grains().front().position.y<grain.position.y&&dust.grains().front().position.y>=dust.grains().front().radius-.0001,"Mineral grit falls and contacts the ground instead of becoming an emissive spark");
    for(int i=0;i<600;++i)dust.advance(1./120.,nullptr,none,{});
    require(dust.puffs().empty()&&dust.grains().empty(),"Dust and grit fade and expire independently of the longer rigid-fragment lifetime");
    for(int i=0;i<50;++i)dust.burst({0,1,0},{0,1,0},{.3,.3,.3},1,i,true);
    require(dust.puffs().size()==24&&dust.grains().size()==256,"Repeated impacts keep bounded independent dust and grit capacities");
    auto puff=dust.puffs().front();auto part=dust_volume(puff);require(!part.emissive&&part.mesh.vertices.front().kind>=13&&part.mesh.vertices.front().kind<14,"Dust proxies carry optical strength as a distinct non-emissive volume type");
    auto tiny=dust_grain(dust.grains().front()).mesh;
    require(orient_fracture_mesh(tiny),"Impact grit uses closed solid mineral geometry");
    AdventurePlayer::Presentation presentation;presentation.showBody=false;presentation.showWeapons=false;presentation.dust={puff,puff};presentation.grit={dust.grains().front()};
    auto batches=batch_presentation(player_presentation(presentation));size_t volumes=0;
    for(const auto& b:batches)if(!b.mesh.vertices.empty()&&b.mesh.vertices.front().kind>=13&&b.mesh.vertices.front().kind<14)++volumes;
    require(volumes==2,"Presentation batching preserves each dust volume rather than merging separate clouds into one solid box");
    dust.clear();require(dust.puffs().empty()&&dust.grains().empty(),"Reset clears all transient destruction feedback");
    // Small closed wall fixture exercises real attack routing without the costly
    // geological subdivision probe, which remains covered separately.
    auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=0;scene->FogDensity=0;scene->Hour=14;
    Character wall;wall.Shape="cube";wall.Id="dust-breakable-wall";wall.GameplayRole="breakable";wall.Z=-2;wall.Color="#555D67";scene->Characters={wall};auto authored=scene->encode();
    auto player=std::make_unique<AdventurePlayer>();player->start(*scene);player->fire_button(true);player->fire_button(false);
    for(int i=0;i<330;++i)player->move(0,0,1./120.);
    auto view=player->presentation();require(!view.dust.empty()&&!view.grit.empty()&&player->scene().Characters.empty()&&scene->encode()==authored,"Real gameplay breakage emits dust and grit while removing only the runtime source");
    auto save=[&](const char* name,AdventurePlayer::Presentation p,SceneData world){p.showWeapons=false;p.showBody=false;p.showTelegraph=false;auto packet=render_scene(world,nullptr,player_presentation(p));atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};
    auto world=player->scene();save("dust-break-early",view,world);auto bare=view;bare.dust.clear();bare.grit.clear();save("dust-break-control",bare,world);
    for(int i=0;i<180;++i)player->move(0,0,1./120.);auto late=player->presentation();save("dust-break-later",late,player->scene());
    require(late.dust.empty()||late.dust.front().opacity()<view.dust.front().opacity(),"Gameplay dust visibly fades after the initial breakup");
    Character occluder;occluder.Shape="cube";occluder.Id="dust-occluder";occluder.X=1.7;occluder.Y=0;occluder.Z=1.2;occluder.Width=8;occluder.Height=8;occluder.Depth=.5;
    // Isolate cloud depth from moving rigid pieces and grit.
    view.fragments.clear();view.grit.clear();world.Characters={occluder};save("dust-wall",view,world);view.dust.clear();save("dust-wall-control",view,world);
    player->start(*scene);require(player->presentation().dust.empty()&&player->presentation().grit.empty()&&player->scene().Characters.size()==1,"Restart clears gameplay dust and restores the authored source");
    atomic_write(folder/"dust-break.starscene",scene->encode());
}
