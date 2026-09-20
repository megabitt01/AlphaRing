#include "D3d11.h"

#include "common.h"

#include "global/Global.h"
#include "mcc/CGameGlobal.h"

namespace AlphaRing::Render::D3d11 {
    static const char* GameName(CGameGlobal::eGame game) {
        switch (game) {
            case CGameGlobal::Halo1: return "Halo1";
            case CGameGlobal::Halo2: return "Halo2";
            case CGameGlobal::Halo3: return "Halo3";
            case CGameGlobal::Halo4: return "Halo4";
            case CGameGlobal::GroundHog: return "GroundHog";
            case CGameGlobal::Halo3ODST: return "Halo3ODST";
            case CGameGlobal::HaloReach: return "HaloReach";
            default: return "Unknown";
        }
    }

    static int s_compositeSlotThisFrame = 0;

    static ID3D11Texture2D* s_cachedBackBuffer = nullptr;
    static UINT s_cachedRtWidth = 0;
    static UINT s_cachedRtHeight = 0;

    static constexpr int kMaxCachedSlots = 8;
    static ID3D11ShaderResourceView* s_knownCompositeSrvBySlot[kMaxCachedSlots] = {};

    void ResetCompositeFrameCounter() {
        s_compositeSlotThisFrame = 0;
        if (s_cachedBackBuffer != nullptr) {
            s_cachedBackBuffer->Release();
            s_cachedBackBuffer = nullptr;
        }
    }

    static bool GetBoundBackbufferSize(ID3D11DeviceContext* p_context, UINT& outWidth, UINT& outHeight) {
        if (s_cachedBackBuffer == nullptr) {
            ID3D11Texture2D* pBB = nullptr;
            if (FAILED(Graphics()->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB)) || pBB == nullptr) {
                return false;
            }
            D3D11_TEXTURE2D_DESC desc;
            pBB->GetDesc(&desc);
            s_cachedBackBuffer = pBB; // reference held until ResetCompositeFrameCounter next frame
            s_cachedRtWidth = desc.Width;
            s_cachedRtHeight = desc.Height;
        }

        ID3D11RenderTargetView* pRTV = nullptr;
        p_context->OMGetRenderTargets(1, &pRTV, nullptr);
        if (pRTV == nullptr) return false;

        ID3D11Resource* pRTResource = nullptr;
        pRTV->GetResource(&pRTResource);
        pRTV->Release();

        bool isBackbuffer = ((void*)s_cachedBackBuffer == (void*)pRTResource);
        if (pRTResource != nullptr) pRTResource->Release();

        if (!isBackbuffer) return false;
        outWidth = s_cachedRtWidth;
        outHeight = s_cachedRtHeight;
        return true;
    }

    struct CompositeSlotClipGuard {
        ID3D11DeviceContext* p_context;
        bool active = false;
        D3D11_VIEWPORT prevViewport = {};
        UINT prevNumViewports = 0;

        CompositeSlotClipGuard(ID3D11DeviceContext* p_ctx, UINT count) : p_context(p_ctx) {
            auto p_splitscreen = AlphaRing::Global::MCC::Splitscreen();
            if (p_splitscreen == nullptr || p_splitscreen->player_count <= 1) return;
            if (count != 6) return;
            if (s_compositeSlotThisFrame >= p_splitscreen->player_count) return;

            UINT rtWidth = 0, rtHeight = 0;
            if (!GetBoundBackbufferSize(p_context, rtWidth, rtHeight)) return;

            UINT sliceHeight = rtHeight / (UINT)p_splitscreen->player_count;
            if (sliceHeight == 0) return;

            int expectedSlot = s_compositeSlotThisFrame; // slot this draw would claim IF it matches

            ID3D11ShaderResourceView* pSrv0 = nullptr;
            p_context->PSGetShaderResources(0, 1, &pSrv0);
            if (pSrv0 == nullptr) return;

            bool matches = false;
            bool cacheable = expectedSlot < kMaxCachedSlots;
            if (cacheable && s_knownCompositeSrvBySlot[expectedSlot] == pSrv0) {
                matches = true;
            } else {
                ID3D11Resource* pRes = nullptr;
                pSrv0->GetResource(&pRes);
                if (pRes != nullptr) {
                    ID3D11Texture2D* pTex = nullptr;
                    if (SUCCEEDED(pRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTex)) && pTex != nullptr) {
                        D3D11_TEXTURE2D_DESC desc;
                        pTex->GetDesc(&desc);
                        UINT diff = desc.Height > sliceHeight ? desc.Height - sliceHeight : sliceHeight - desc.Height;
                        matches = diff * 10 < sliceHeight; // within 10%
                        pTex->Release();
                    }
                    pRes->Release();
                }
                if (matches && cacheable) {
                    if (s_knownCompositeSrvBySlot[expectedSlot] != nullptr) {
                        s_knownCompositeSrvBySlot[expectedSlot]->Release();
                    }
                    s_knownCompositeSrvBySlot[expectedSlot] = pSrv0;
                    pSrv0->AddRef(); // own a reference for the cache, independent of the one below
                }
            }
            pSrv0->Release();

            if (!matches) return;

            int slot = expectedSlot;
            s_compositeSlotThisFrame++;

            UINT startY = (UINT)slot * sliceHeight;
            UINT endY = (slot == p_splitscreen->player_count - 1) ? rtHeight : (startY + sliceHeight);

            prevNumViewports = 1;
            p_context->RSGetViewports(&prevNumViewports, &prevViewport);

            D3D11_VIEWPORT vp{};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = (float)startY;
            vp.Width = (float)rtWidth;
            vp.Height = (float)(endY - startY);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            p_context->RSSetViewports(1, &vp);

            active = true;
        }

        ~CompositeSlotClipGuard() {
            if (!active) return;

            if (prevNumViewports > 0) {
                p_context->RSSetViewports(prevNumViewports, &prevViewport);
            } else {
                p_context->RSSetViewports(0, nullptr);
            }
        }
    };

    DefDetourFunction(HRESULT, __stdcall, DrawIndexed, ID3D11DeviceContext* p_context,
                      const UINT IndexCount, const UINT StartIndexLocation, const INT  BaseVertexLocation) {
        if (AlphaRing::Global::Global()->wireframe) {
            Graphics()->SetWireframe();
#ifdef NEW_WIREFRAME
            D3D11_PRIMITIVE_TOPOLOGY topology;
            p_context->IAGetPrimitiveTopology(&topology);

            p_context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);

            p_context->IASetPrimitiveTopology(topology);
#endif
        }

        auto pGameGlobal = GameGlobal();
        bool inHalo2 = pGameGlobal != nullptr && pGameGlobal->current_game == CGameGlobal::Halo2;

        {
            static CGameGlobal::eGame last_logged_game = (CGameGlobal::eGame)-1;
            if (pGameGlobal != nullptr && pGameGlobal->current_game != last_logged_game) {
                last_logged_game = pGameGlobal->current_game;
                LOG_INFO("D3D11: active game changed to {}", GameName(last_logged_game));
            }
        }

        HRESULT result;
        {
            CompositeSlotClipGuard clip(p_context, inHalo2 ? IndexCount : 0);
            result = ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);
        }
        return result;
    }

    bool CreateHook() {
        return Hook::Detour({
            {GetFunction(73), DrawIndexed, (void **) &ppOriginal_DrawIndexed},
        });
    }
}
