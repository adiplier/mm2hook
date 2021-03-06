#pragma once
#include "mm2_common.h"
#include "mm2_gfx.h"

namespace MM2
{
    // Forward declarations
    class modModel;
    class modPackage;
    class modShader;
    class modStatic;

    // External declarations
    extern class Stream;
    extern class gfxPacket;
    extern class gfxPacketList;
    extern class gfxTexture;

    class modShader {
    public:
        gfxTexture *Texture;
        gfxMaterial *Material;
    public:
        static ageHook::Type<gfxMaterial *> sm_Materials;
        static ageHook::Type<int> sm_StaticMaterialCount;

        static AGE_API void BeginEnvMap(gfxTexture *tex, const Matrix34 &mtx)
                                                                { return ageHook::StaticThunk<0x4A41B0>::Call<void>(tex, &mtx); }
        static AGE_API void EndEnvMap(void)                     { return ageHook::StaticThunk<0x4A4420>::Call<void>(); }
        static AGE_API modShader ** LoadShaderSet(Stream *stream, int *a2, int *a3, bool a4)
                                                                { return ageHook::StaticThunk<0x4A3F60>::Call<modShader **>(stream, a2, a3, a4); }
        static AGE_API gfxMaterial * AddStaticMaterial(const gfxMaterial &mtl)
                                                                { return ageHook::StaticThunk<0x4A3930>::Call<gfxMaterial *>(&mtl); }
        static AGE_API void KillAll(void)                       { return ageHook::StaticThunk<0x4A3B20>::Call<void>(); }
        
        AGE_API void Load(Stream *stream, bool a2)              { return ageHook::Thunk<0x4A3B30>::Call<void>(this, stream, a2); }
        AGE_API void PreLoad(void)                              { return ageHook::Thunk<0x4A40C0>::Call<void>(this); }
    };

    class modPackage {
    private:
        modPackage *next;
        Stream *stream;
        char file[32];
        uint magic;
        uint fileSize;
    public:
        AGE_API modPackage(void) {
            PUSH_VTABLE();
            ageHook::Thunk<0x4A46D0>::Call<void>(this);
            POP_VTABLE();
        }

        AGE_API ~modPackage(void) {
            PUSH_VTABLE();
            ageHook::Thunk<0x4A46F0>::Call<void>(this);
            POP_VTABLE();
        }

        AGE_API bool Open(LPCSTR dir, LPCSTR filename)          { return ageHook::Thunk<0x4A4700>::Call<bool>(this, dir, filename); }
        AGE_API void Close(void)                                { return ageHook::Thunk<0x4A4790>::Call<void>(this); }
        AGE_API Stream * OpenFile(LPCSTR file)                  { return ageHook::Thunk<0x4A47B0>::Call<Stream *>(this, file); }
        AGE_API void CloseFile(void)                            { return ageHook::Thunk<0x4A4800>::Call<void>(this); }
        AGE_API void SkipTo(LPCSTR file)                        { return ageHook::Thunk<0x4A48D0>::Call<void>(this, file); }
        AGE_API void Skip(void)                                 { return ageHook::Thunk<0x4A4970>::Call<void>(this); }
    };

    class modStatic {
    public:
        uint8_t PacketCount;
        uint8_t Flags;
        uint16_t FvfFlags;
        uint8_t *ShaderIndices;
        gfxPacket **ppPackets;
        gfxPacketList **ppPacketLists;

    public:
        AGE_API int GetTriCount(void) const	                    { return ageHook::Thunk<0x4A4DE0>::Call<int>(this); }
        AGE_API int GetAdjunctCount(void) const	                { return ageHook::Thunk<0x4A4DB0>::Call<int>(this); }
        AGE_API void CopyFrom(const modStatic *mod)	            { return ageHook::Thunk<0x4A4D60>::Call<void>(this, mod); }
        AGE_API modStatic * Clone(void) const	                { return ageHook::Thunk<0x4A4CA0>::Call<modStatic *>(this); }
        AGE_API void Optimize(modShader *shader)	            { return ageHook::Thunk<0x4A49A0>::Call<void>(this, shader); }
        AGE_API void Draw(modShader *shader) const	            { return ageHook::Thunk<0x4A4550>::Call<void>(this, shader); }
        AGE_API void DrawNoAlpha(modShader *shader) const	    { return ageHook::Thunk<0x4A4A20>::Call<void>(this, shader); }
        AGE_API void DrawEnvMapped(modShader *shader, gfxTexture *tex, float a3) const
                                                                { return ageHook::Thunk<0x4A4A50>::Call<void>(this, shader, tex, a3); }
        AGE_API void DrawOrthoMapped(modShader *shader, gfxTexture *tex, float a3, uint a4) const
                                                                { return ageHook::Thunk<0x4A4B30>::Call<void>(this, shader, tex, a3, a4); }
        AGE_API void DrawWithTexGenAndTexMatrix(void) const	    { return ageHook::Thunk<0x4A4C50>::Call<void>(this); }
    };

    class modModel {
    private:
        byte numMaterials; // Shader + Packet count
        byte numMatrices;
        byte flags;
        byte reserved; // unused (padding)

        modShader *shaders;
        gfxPacket **packets;
    public:
        static AGE_API bool ModelAlreadyLoaded(LPCSTR model)    { return ageHook::StaticThunk<0x597A20>::Call<bool>(model); }
        static AGE_API void DeleteModelHash(LPCSTR model)       { return ageHook::StaticThunk<0x597A40>::Call<void>(model); }

        AGE_API void GetBoundingBox(Vector3 &a1, Vector3 &a2, Matrix34 *a3) const
                                                                { return ageHook::Thunk<0x597FB0>::Call<void>(this, &a1, &a2, a3); }
        AGE_API int GetAdjunctCount(void) const                 { return ageHook::Thunk<0x598190>::Call<int>(this); }
        AGE_API Vector3 & GetPosition(Vector3 &outPos, int a2) const
                                                                { return ageHook::Thunk<0x598230>::Call<Vector3 &>(this, &outPos, a2); }
        AGE_API void SetPosition(const Vector3 &pos, int a2) const
                                                                { return ageHook::Thunk<0x598290>::Call<void>(this, &pos, a2); }
        AGE_API void Draw(const Matrix44 *mtx, modShader *shader, uint a3) const
                                                                { return ageHook::Thunk<0x597D00>::Call<void>(this, mtx, shader, a3); }
        AGE_API void DrawPlain(const Matrix44 *mtx, uint a2) const
                                                                { return ageHook::Thunk<0x597F00>::Call<void>(this, mtx, a2); }
        AGE_API void DrawWithTexGenCoords(const Matrix44 *mtx, gfxTexture &tex, uint a3) const
                                                                { return ageHook::Thunk<0x597EA0>::Call<void>(this, mtx, &tex, a3); }
        AGE_API modShader * CopyShaders(void) const             { return ageHook::Thunk<0x5981C0>::Call<modShader *>(this); }
    };

    AGE_EXT modStatic * modGetStatic(LPCSTR file, float &a2, bool a3);
    AGE_EXT modStatic * modGetStatic(LPCSTR file, void(*a2)(Vector3 &, void *), void *a3, bool a4);
    AGE_EXT modModel * modGetModel(LPCSTR filename, uint a2, bool a3, bool a4);
    AGE_EXT void modConvertModel(LPCSTR filename, LPCSTR newFilename, bool a3);
    AGE_EXT bool GetPivot(Matrix34 &, LPCSTR basename, LPCSTR file);

    template<>
    void luaAddModule<module_model>(LuaState L) {

    }
}