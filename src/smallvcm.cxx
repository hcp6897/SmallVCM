/*
 * This is published under Apache 2.0
 */

#include <vector>
#include <cmath>
#include <time.h>
#include "math.hxx"
#include "ray.hxx"
#include "geometry.hxx"
#include "camera.hxx"
#include "framebuffer.hxx"
#include "scene.hxx"
#include "eyelight.hxx"
#include "pathtracer.hxx"
#include "bxdf.hxx"
#include "vertexcm.hxx"

#include <omp.h>
#include <string>

struct Config
{
    enum Algorithm
    {
        kEyeLight,
        kPathTracing,
        kLightTracing,
        kProgressivePhotonMapping,
        kBidirectionalPhotonMapping,
        kBidirectionalPathTracing,
        kVertexConnectionMerging,
        kAlgorithmMax
    };

    const char* GetName()
    {
        static const char* algorithmNames[7] = {
            "Eye Light (L.N, DotLN)",
            "Path Tracing",
            "Light Tracing",
            "Progressive Photon Mapping",
            "Bidirectional Photon Mapping",
            "Bidirectional Path Tracing",
            "Vertex Connection Merging"
        };

        if(mAlgorithm < 0 || mAlgorithm > 7)
            return "Uknown algorithm";
        return algorithmNames[mAlgorithm];
    }

    const char* GetAcronym()
    {
        static const char* algorithmNames[7] = {
            "el", "pt", "lt", "ppm", "bpm", "bpt", "vcm" };

        if(mAlgorithm < 0 || mAlgorithm > 7)
            return "uknown";
        return algorithmNames[mAlgorithm];
    }

    const Scene *mScene;
    Algorithm   mAlgorithm;
    int         mIterations;
    Framebuffer *mFramebuffer;
    int         mNumThreads;
    int         mBaseSeed;
    uint        mMaxPathLength;
};


float render(const Config &aConfig)
{
    omp_set_num_threads(aConfig.mNumThreads);

    typedef AbstractRenderer* AbstractRendererPtr;
    AbstractRendererPtr *renderers;
    renderers = new AbstractRendererPtr[aConfig.mNumThreads];

    int iterations = aConfig.mIterations;
    switch(aConfig.mAlgorithm)
    {
    case Config::kEyeLight:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new EyeLight(*aConfig.mScene);
        // iterations have no meaning for kEyeLight
        iterations = 1;
        break;
    case Config::kPathTracing:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new PathTracer(*aConfig.mScene,
            aConfig.mBaseSeed + i);
        break;
    case Config::kLightTracing:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new VertexCM(*aConfig.mScene,
            VertexCM::kLightTrace, aConfig.mBaseSeed + i);
        break;
    case Config::kProgressivePhotonMapping:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new VertexCM(*aConfig.mScene,
            VertexCM::kPpm, aConfig.mBaseSeed + i);
        break;
    case Config::kBidirectionalPhotonMapping:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new VertexCM(*aConfig.mScene,
            VertexCM::kBpm, aConfig.mBaseSeed + i);
        break;
    case Config::kBidirectionalPathTracing:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new VertexCM(*aConfig.mScene,
            VertexCM::kBpt, aConfig.mBaseSeed + i);
        break;
    case Config::kVertexConnectionMerging:
        for(int i=0; i<aConfig.mNumThreads; i++)
            renderers[i] = new VertexCM(*aConfig.mScene,
            VertexCM::kVcm, aConfig.mBaseSeed + i);
        break;
    }

    for(int i=0; i<aConfig.mNumThreads; i++)
        renderers[i]->mMaxPathLength = aConfig.mMaxPathLength;

    clock_t startT = clock();
#pragma omp parallel for
    for(int iter=0; iter < iterations; iter++)
    {
        int threadId = omp_get_thread_num();
        renderers[threadId]->RunIteration(iter);
    }
    clock_t endT = clock();

    int usedRenderers = 0;
    for(int i=0; i<aConfig.mNumThreads; i++)
    {
        if(!renderers[i]->WasUsed()) continue;
        if(usedRenderers == 0)
        {
            renderers[i]->GetFramebuffer(*aConfig.mFramebuffer);
        }
        else
        {
            Framebuffer tmp;
            renderers[i]->GetFramebuffer(tmp);
            aConfig.mFramebuffer->Add(tmp);
        }
        usedRenderers++;
    }

    aConfig.mFramebuffer->Scale(1.f / usedRenderers);

    for(int i=0; i<aConfig.mNumThreads; i++)
        delete renderers[i];
    delete [] renderers;

    return float(endT - startT) / CLOCKS_PER_SEC;
}

struct SceneConfig
{
    SceneConfig(){};
    SceneConfig(uint aMask, const char* aName, const char* aAcronym)
        : mMask(aMask),
        mName(aName),
        mAcronym(aAcronym)
    {}

    uint       mMask;
    const char *mName;
    const char *mAcronym;
};

int main(int argc, const char *argv[])
{
    int base_iterations = 1;
    if(argc > 1)
        base_iterations = atoi(argv[1]);

    const int numThreads = std::max(1, omp_get_num_procs()-1);
    printf("Using %d threads\n", numThreads);

    SceneConfig sceneConfigs[] = {
        SceneConfig(Scene::kLightCeiling,    "Empty + Ceiling", "ec"),
        SceneConfig(Scene::kLightSun,        "Empty + Sun", "es"),
        SceneConfig(Scene::kLightPoint,      "Empty + Point", "ep"),
        SceneConfig(Scene::kLightBackground, "Empty + Background", "eb"),

        SceneConfig(Scene::kBothSmallBalls | Scene::kLightCeiling,    "Small balls + Ceiling", "sbc"),
        SceneConfig(Scene::kBothSmallBalls | Scene::kLightSun,        "Small balls + Sun", "sbs"),
        SceneConfig(Scene::kBothSmallBalls | Scene::kLightPoint,      "Small balls + Point", "sbp"),
        SceneConfig(Scene::kBothSmallBalls | Scene::kLightBackground, "Small balls + Background", "sbb"),

        SceneConfig(Scene::kBallLargeMirror | Scene::kLightCeiling,    "Large mirror ball + Ceiling", "lbc"),
        SceneConfig(Scene::kBallLargeMirror | Scene::kLightSun,        "Large mirror ball + Sun", "lbs"),
        SceneConfig(Scene::kBallLargeMirror | Scene::kLightPoint,      "Large mirror ball + Point", "lbp"),
        SceneConfig(Scene::kBallLargeMirror | Scene::kLightBackground, "Large mirror ball + Background", "lbb"),
    };

    const int sceneConfigCount = sizeof(sceneConfigs) / sizeof(SceneConfig);

    Framebuffer fbuffer;
    Config      config;
    config.mIterations    = 10;
    config.mFramebuffer   = &fbuffer;
    config.mNumThreads    = numThreads;
    config.mBaseSeed      = 1234;
    config.mMaxPathLength = 10;

    std::ofstream html("report.html");
    int thumbnailSize = 128;

    int algorithmMask[7] = {1,1,1,1,1,1,1};

    for(int sceneId2 = 0; sceneId2 < sceneConfigCount * 2; sceneId2++)
    {
        int sceneId = sceneId2 % sceneConfigCount;
        uint mask = sceneConfigs[sceneId].mMask;
        if(sceneId < sceneConfigCount)
            mask |= Scene::kGlossyFloor;
        Scene scene;
        scene.LoadCornellBox(Vec2i(256), mask);
        scene.BuildSceneSphere();
        config.mScene = &scene;
        std::string sceneFilename(sceneConfigs[sceneId].mAcronym);

        html << "<table>" << std::endl;
        html << "<tr>" << std::endl;
        if((mask & Scene::kGlossyFloor) != 0)
            html << "<h2> Glossy " << sceneConfigs[sceneId].mName << "</h2>" << std::endl;
        else
            html << "<h2>" << sceneConfigs[sceneId].mName << "</h2>" << std::endl;
        html << "</tr>" << std::endl;

        printf("Scene: %s\n", sceneConfigs[sceneId].mName);
        html << "<tr>" << std::endl;
        for(uint algId = 0; algId < Config::kAlgorithmMax; algId++)
        {
            if(!algorithmMask[algId]) continue;
            config.mAlgorithm = Config::Algorithm(algId);
            printf("Running %s... ", config.GetName());
            fflush(stdout);
            float time = render(config);
            printf("done in %g s\n", time);
            std::string filename = sceneFilename + "_" +
                config.GetAcronym() + ".bmp";

            // Html output
            fbuffer.SaveBMP(filename.c_str(), 2.2f);
            html << "<td> <a href=\"" << filename << "\">"
                << "<img src=\"" << filename << "\" "
                << "alt=\"" << config.GetName() << " (" << time << " s)\" "
                << "height=\"" << thumbnailSize << "\" "
                << "width=\"" << thumbnailSize << "\" />"
                << "</a><br/>" << std::endl;
            html << config.GetAcronym()
                << " (" << time << " s)</td>" << std::endl;
        }
        html << "</tr>" << std::endl;
        html << "</table>" << std::endl;
    }
}
