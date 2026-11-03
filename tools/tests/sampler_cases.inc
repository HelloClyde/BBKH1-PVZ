// Included only in the isolated sampler oracle, never in the game build.
static void SamplerAdditionalCases(unsigned& direct,unsigned& draws) {
    std::vector<uint32_t> dense(3072);
    for(unsigned i=0;i<dense.size();++i)dense[i]=i*2246822519u;
    const uint32_t*pages[]={dense.data(),dense.data()+1024,dense.data()+2048};
    SWHelper::SWTextureInfo info{};info.pTexture=dense.data();info.pitch=64;
    info.clampLeft=3;info.clampRight=61;info.clampTop=2;info.clampBottom=43;
    for(bool paged:{false,true}){
        info.pages=paged?pages:nullptr;
        for(unsigned i=0;i<dense.size();++i){
            Check(info.ReadScreen(i)==info.Read(i),"inline dense/page reader changed a pixel");++direct;
        }
    }
    info.pages=nullptr;
    for(int y=-3;y<48;++y)for(int x=-3;x<70;++x){
        Check(info.ReadScreenClamped(x,y)==info.ReadClamped(x,y),"inline cell clamp changed a pixel");++direct;
    }
    // Exercise actual normal nearest AND original linear screen shaders, with
    // full alpha range, vertex/global colors and clipping. Synthetic images have
    // no storage descriptor, so no reduced-pack filtering can hide linear code.
    MemoryImage texture(gLawnApp);texture.Create(17,13);
    MemoryImage destination(gLawnApp);destination.Create(211,143);destination.SetImageMode(false,false);
    for(bool opaque:{false,true}){
        auto bits=texture.GetBits();for(unsigned i=0;i<17*13;++i)bits[i]=((opaque?255u:i%256u)<<24)|((i*0x15779bu)&0xffffffu);texture.BitsChanged();texture.CommitBits();
        for(bool linear:{false,true})for(bool vertex:{false,true})for(Color color:{Color::White,Color(101,179,231,137)})for(bool clipped:{false,true}){
            std::vector<uint32_t> old;
            for(bool fast:{false,true}){
                pvzh1_sampler_test_enabled=fast;Background(&destination);
                SWHelper::XYZStruct v[4]={{-13.125f,7.5f,0.f,0.f,(long)0xff31a9e3u},{207.75f,-5.125f,1.f,0.f,(long)0xa77fdfa1u},{5.25f,142.875f,0.f,1.f,(long)0x7fffc749u},{217.5f,136.5f,1.f,1.f,(long)0xe13781f9u}};
                const Rect clip=clipped?Rect(11,13,173,113):Rect(0,0,211,143);
                SWHelper::SWDrawShape(v,4,&texture,color,0,clip,destination.GetBits(),211*4,0x888,linear,vertex);
                if(!fast)old=Pixels(&destination);else Check(old==Pixels(&destination),"actual nearest/linear vertex/global-alpha screen pixels changed");
            }
            ++draws;
        }
    }
    // The new shader selection is legal only for a genuine all-255 storage
    // source. Validate colored/alpha draws of the actual opaque Selector base.
    GLImage base(gLawnApp->mGLInterface.get());
    Check(H1ImageCacheDescribe(&base,"reanim/selectorscreen_bg",true,0xffffff),"opaque actual BG descriptor missing");
    for(bool linear:{false,true})for(Color color:{Color::White,Color(101,179,231,137),Color(239,193,41,8)}){
        std::vector<uint32_t> old;
        for(bool fast:{false,true}){
            pvzh1_sampler_test_enabled=fast;Background(&destination);SexyTransform2D matrix;matrix.Scale(1.89f,1.71f);matrix.Translate(-3.125f,9.75f);
            H1DrawTransformed(&destination,&base,Rect(0,0,base.mWidth,base.mHeight),Rect(7,11,191,121),color,0,matrix,linear);
            if(!fast)old=Pixels(&destination);else Check(old==Pixels(&destination),"opaque storage shader global-color/alpha pixels changed");
        }
        ++draws;
    }
    pvzh1_sampler_test_enabled=true;
}
