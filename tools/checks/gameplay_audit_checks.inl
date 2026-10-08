// Gameplay audit repairs (08#1 camera boom, 08#4 lightning chain, 08#5 tab-targeting of routed breakables).
static void gameplay_audit_checks(const auto& require){
    {
        // 08#1: effect shapes are not solid, so the camera boom ignores them as it ignores keys; a real wall still stops it.
        for(const char* shape:{"fire","light","craxis-smoke","grass","lava"}){
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
            Character effect;effect.Shape=shape;effect.Width=effect.Height=effect.Depth=5;effect.Z=2;level.Characters={effect};
            AdventurePlayer p;p.start(level);
            require(p.camera_eye().z>p.view_position().z+4.99,(std::string("08#1: The camera boom is not shortened by a non-solid ")+shape+" object behind the player").c_str());
        }
        SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
        Character wall;wall.Shape="cube";wall.Width=5;wall.Height=5;wall.Depth=.2;wall.Z=2;level.Characters={wall};
        AdventurePlayer p;p.start(level);
        require(p.camera_eye().z<2,"08#1: A solid wall behind the player still stops the camera boom");
    }
}
