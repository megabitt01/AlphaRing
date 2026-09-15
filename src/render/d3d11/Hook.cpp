#include "D3d11.h"

#include <cstdio>
#include <cstdlib>

#include "common.h"

#include "global/Global.h"
#include "mcc/CGameGlobal.h"

namespace AlphaRing::Render::D3d11 {
    // Ruled out (2026-09-14): HLSL dynamic shader linkage (ID3D11ClassInstance) was suspected
    // as the cause of Halo 2 Anniversary's Linux/Proton black 3D viewport, since DXVK has a
    // documented gap there and the symptom (HUD fine, 3D black) matches other DXVK titles.
    // Tested on real hardware: no *SetShader call ever carried a non-empty ppClassInstances.
    // That path is not it.
    //
    // Also corrected (2026-09-14): the campaign's in-game "Anniversary Graphics" toggle does
    // NOT switch current_game to GroundHog. A live log confirmed current_game stays Halo2
    // (halo2.dll) in both Classic and Anniversary graphics modes during campaign play; GroundHog
    // is something else (most likely the separate H2A multiplayer client). Diagnostics below are
    // gated on Halo2, not GroundHog.
    //
    // Also reverted (2026-09-14): hooking OMSetRenderTargets/ClearRenderTargetView/
    // ResolveSubresource directly (extra MinHook detours on those vtable slots) caused a fatal
    // error loading Halo 2 under Wine that did not happen before those hooks existed — most
    // likely MinHook's trampoline relocation misbehaving on whatever DXVK's implementations of
    // those particular (very hot-path, possibly very short) functions look like. Do not re-add
    // hooks on those three. Instead, render-target info is sampled by calling OMGetRenderTargets
    // from inside the already-stable DrawIndexed hook, throttled to once/sec, which adds no new
    // vtable hooks at all.

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

    static bool InHalo2() {
        auto pGameGlobal = GameGlobal();
        return pGameGlobal != nullptr && pGameGlobal->current_game == CGameGlobal::Halo2;
    }

    // Reads back a small block from the CENTER of a texture (not a corner — a corner is very
    // likely to land on a static UI/letterbox/background pixel that's legitimately constant
    // regardless of what the 3D scene is doing, which is exactly what the first round of this
    // data showed: identical repeating byte patterns across totally different points in time).
    // The center is far more likely to intersect actual rendered gameplay content. Uses a
    // STAGING copy + Map, not a hook — just extra CPU-side work in an already-throttled
    // (once/sec) call site, so it carries none of the hot-path hooking risk that caused the
    // earlier crash. This is the ground truth a call-trace can't give us: whether a suspect
    // buffer genuinely never got anything drawn into it, or holds real (varied, not just
    // nonzero) data that isn't reaching the screen.
    static void LogTextureContentSample(ID3D11Texture2D* pTex, const D3D11_TEXTURE2D_DESC& srcDesc) {
        UINT sampleW = srcDesc.Width < 64 ? srcDesc.Width : 64;
        UINT sampleH = srcDesc.Height < 64 ? srcDesc.Height : 64;
        if (sampleW == 0 || sampleH == 0) return;

        UINT originX = (srcDesc.Width > sampleW) ? (srcDesc.Width - sampleW) / 2 : 0;
        UINT originY = (srcDesc.Height > sampleH) ? (srcDesc.Height - sampleH) / 2 : 0;

        D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
        stagingDesc.Width = sampleW;
        stagingDesc.Height = sampleH;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ID3D11Texture2D* pStaging = nullptr;
        if (FAILED(Graphics()->pDevice->CreateTexture2D(&stagingDesc, nullptr, &pStaging)) || pStaging == nullptr) {
            LOG_INFO("D3D11 [Halo2]: content sample: failed to create staging texture");
            return;
        }

        D3D11_BOX box { originX, originY, 0, originX + sampleW, originY + sampleH, 1 };
        Graphics()->pContext->CopySubresourceRegion(pStaging, 0, 0, 0, 0, pTex, 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(Graphics()->pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
            bool anyNonZero = false;
            UINT8 firstBytes[16] = {};
            auto* pRow0 = (const UINT8*)mapped.pData;
            for (UINT y = 0; y < sampleH && !anyNonZero; y++) {
                auto* pRow = (const UINT8*)mapped.pData + (size_t)y * mapped.RowPitch;
                for (UINT x = 0; x < mapped.RowPitch; x++) {
                    if (pRow[x] != 0) { anyNonZero = true; break; }
                }
            }
            memcpy(firstBytes, pRow0, sizeof(firstBytes) < mapped.RowPitch ? sizeof(firstBytes) : mapped.RowPitch);

            // "Uniform" = every row byte-identical to row 0, i.e. no vertical detail at all —
            // the signature of a flat clear color / background, as opposed to actual rendered
            // geometry, which will essentially never produce perfectly repeating rows.
            bool uniform = true;
            for (UINT y = 1; y < sampleH && uniform; y++) {
                auto* pRow = (const UINT8*)mapped.pData + (size_t)y * mapped.RowPitch;
                if (memcmp(pRow0, pRow, mapped.RowPitch) != 0) uniform = false;
            }

            LOG_INFO("D3D11 [Halo2]: content sample {}x{} @ center: {} {} (first bytes: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x})",
                      sampleW, sampleH, anyNonZero ? "HAS non-zero data" : "ALL ZERO",
                      uniform ? "[UNIFORM/flat]" : "[VARIED]",
                      firstBytes[0], firstBytes[1], firstBytes[2], firstBytes[3],
                      firstBytes[4], firstBytes[5], firstBytes[6], firstBytes[7]);

            Graphics()->pContext->Unmap(pStaging, 0);
        } else {
            LOG_INFO("D3D11 [Halo2]: content sample: Map failed");
        }

        pStaging->Release();
    }

    // LogViewportChange/LogScissorChange (removed 2026-09-15): logged every distinct
    // viewport/scissor rect on every DrawIndexed call. That question is closed — every
    // color-target viewport and scissor rect was conclusively shown to always be
    // (0,0,full-size-of-its-own-target), ruling out both as the splitscreen confinement/stretch
    // mechanism. Left on, this was ~99% of total log volume (94.5k of 95.7k lines in one capture)
    // for no further signal. Do not re-add without a concrete new reason to re-examine
    // viewport/scissor state specifically.

    static void LogBoundRenderTarget(ID3D11DeviceContext* p_context) {
        ID3D11RenderTargetView* pRTV = nullptr;
        ID3D11DepthStencilView* pDSV = nullptr;

        p_context->OMGetRenderTargets(1, &pRTV, &pDSV);

        LOG_INFO("D3D11 [Halo2]: bound rtv0={} dsv={}", (void*)pRTV, (void*)pDSV);

        if (pRTV != nullptr) {
            ID3D11Resource* pResource = nullptr;
            pRTV->GetResource(&pResource);
            if (pResource != nullptr) {
                // Identify whether the currently bound target IS the real swapchain backbuffer
                // (what actually ends up on screen), as opposed to an intermediate/offscreen
                // buffer. QueryInterface for the same D3D11 object always yields the same
                // pointer for a given interface, so comparing against a freshly-fetched
                // backbuffer pointer is a reliable identity check.
                bool isBackbuffer = false;
                ID3D11Texture2D* pBackBuffer = nullptr;
                if (SUCCEEDED(Graphics()->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)) && pBackBuffer != nullptr) {
                    isBackbuffer = ((void*)pBackBuffer == (void*)pResource);
                    pBackBuffer->Release();
                }

                ID3D11Texture2D* pTex = nullptr;
                if (SUCCEEDED(pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTex)) && pTex != nullptr) {
                    D3D11_TEXTURE2D_DESC desc;
                    pTex->GetDesc(&desc);
                    LOG_INFO("D3D11 [Halo2]: rtv0 texture {}x{} format={} sampleCount={} bindFlags={:#x} isBackbuffer={}",
                              desc.Width, desc.Height, (int)desc.Format, desc.SampleDesc.Count, desc.BindFlags, isBackbuffer);
                    LogTextureContentSample(pTex, desc);
                    pTex->Release();
                }
                pResource->Release();
            }
            pRTV->Release();
        }

        if (pDSV != nullptr) pDSV->Release();
    }

    // Reads back up to maxBytes starting at startByte of a D3D11 buffer (vertex/index/constant
    // buffer) via a STAGING copy + Map, same technique as LogTextureContentSample above, and logs
    // the first few floats. For a full-screen composite quad, the vertex buffer's positions/UVs
    // (or a constant buffer feeding the vertex/pixel shader) is exactly where a per-player
    // destination-rect would live, so this is the ground truth for the "what parameterizes this
    // draw's placement" question that static disassembly couldn't answer. startByte matters: a
    // vertex buffer bound with a nonzero offset (IAGetVertexBuffers) is commonly a large
    // shared/suballocated buffer, and byte 0 of it has nothing to do with what this particular
    // draw call actually reads.
    static void LogBufferFloatSample(ID3D11Buffer* pBuffer, UINT startByte, UINT maxBytes, const char* label) {
        if (pBuffer == nullptr) {
            LOG_INFO("D3D11 [Halo2]: composite {}: null", label);
            return;
        }

        D3D11_BUFFER_DESC srcDesc;
        pBuffer->GetDesc(&srcDesc);

        if (startByte >= srcDesc.ByteWidth) {
            LOG_INFO("D3D11 [Halo2]: composite {}: startByte {} >= buffer size {}", label, startByte, srcDesc.ByteWidth);
            return;
        }

        // Only stage+copy the bytes actually being sampled, not the whole buffer — a vertex
        // buffer here can be a large shared/suballocated one (tens of MB), and a full CopyResource
        // on every throttled sample was a real GPU-copy cost for data we never looked at.
        UINT available = srcDesc.ByteWidth - startByte;
        UINT sampleBytes = available < maxBytes ? available : maxBytes;
        if (sampleBytes == 0) return;

        D3D11_BUFFER_DESC stagingDesc = srcDesc;
        stagingDesc.ByteWidth = sampleBytes;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ID3D11Buffer* pStaging = nullptr;
        if (FAILED(Graphics()->pDevice->CreateBuffer(&stagingDesc, nullptr, &pStaging)) || pStaging == nullptr) {
            LOG_INFO("D3D11 [Halo2]: composite {}: failed to create staging buffer (size={})", label, srcDesc.ByteWidth);
            return;
        }

        D3D11_BOX box { startByte, 0, 0, startByte + sampleBytes, 1, 1 };
        Graphics()->pContext->CopySubresourceRegion(pStaging, 0, 0, 0, 0, pBuffer, 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(Graphics()->pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
            UINT floatCount = sampleBytes / sizeof(float);
            if (floatCount > 16) floatCount = 16;
            auto* pFloats = (const float*)mapped.pData;

            char line[512] = {};
            int pos = 0;
            for (UINT i = 0; i < floatCount; i++) {
                int written = snprintf(line + pos, sizeof(line) - pos, "%.3f ", pFloats[i]);
                if (written <= 0) break;
                pos += written;
                if (pos >= (int)sizeof(line) - 16) break;
            }

            LOG_INFO("D3D11 [Halo2]: composite {} (size={} bytes, {} floats shown): {}",
                      label, srcDesc.ByteWidth, floatCount, line);

            Graphics()->pContext->Unmap(pStaging, 0);
        } else {
            LOG_INFO("D3D11 [Halo2]: composite {}: Map failed", label);
        }

        pStaging->Release();
    }

    // Returns the backbuffer's own size if (and only if) the currently bound render target IS
    // the real swapchain backbuffer — shared by the composite logger and the splitscreen-composite
    // fix below, both of which only care about draws that land directly on the backbuffer.
    static bool GetBoundBackbufferSize(ID3D11DeviceContext* p_context, UINT& outWidth, UINT& outHeight) {
        ID3D11RenderTargetView* pRTV = nullptr;
        p_context->OMGetRenderTargets(1, &pRTV, nullptr);
        if (pRTV == nullptr) return false;

        bool isBackbuffer = false;
        UINT rtWidth = 0, rtHeight = 0;
        ID3D11Resource* pRTResource = nullptr;
        pRTV->GetResource(&pRTResource);
        if (pRTResource != nullptr) {
            ID3D11Texture2D* pBackBuffer = nullptr;
            if (SUCCEEDED(Graphics()->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)) && pBackBuffer != nullptr) {
                isBackbuffer = ((void*)pBackBuffer == (void*)pRTResource);
                pBackBuffer->Release();
            }
            if (isBackbuffer) {
                ID3D11Texture2D* pRTTex = nullptr;
                if (SUCCEEDED(pRTResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pRTTex)) && pRTTex != nullptr) {
                    D3D11_TEXTURE2D_DESC rtDesc;
                    pRTTex->GetDesc(&rtDesc);
                    rtWidth = rtDesc.Width;
                    rtHeight = rtDesc.Height;
                    pRTTex->Release();
                }
            }
            pRTResource->Release();
        }
        pRTV->Release();

        if (!isBackbuffer || rtWidth == 0 || rtHeight == 0) return false;
        outWidth = rtWidth;
        outHeight = rtHeight;
        return true;
    }

    // Fires on any Draw/DrawIndexed call that (a) targets the real swapchain backbuffer,
    // (b) draws a tiny vertex/index count (<=6 — a triangle, quad-as-strip, or quad-as-two-tris),
    // and (c) samples at least one input texture whose size meaningfully differs from the
    // backbuffer's own size — the signature of an upscale/composite blit (e.g. a half-height
    // per-player texture stretched into the full backbuffer), as opposed to a same-size
    // post-process pass (e.g. tonemapping) that isn't what we're chasing. That combination is
    // narrow enough to isolate from the thousands of ordinary scene-geometry draws per frame.
    // Static analysis (Ghidra) could not reliably identify the composite function by
    // name/address in a 15MB symbol-less binary, so this pulls the actual vertex/constant-buffer
    // data straight from the GPU at the moment compositing happens — directly answering what
    // parameterizes each player's destination rect, and letting an AA-on vs AA-off capture
    // comparison show what differs. Throttled (via the shared lastLogTime) to keep log volume
    // bounded even while this condition holds every frame.
    static void LogCompositeDrawCandidate(ID3D11DeviceContext* p_context, const char* drawKind, UINT count, UINT startLocation) {
        static ULONGLONG lastLogTime = 0;

        if (count == 0 || count > 6) return;

        UINT rtWidth = 0, rtHeight = 0;
        if (!GetBoundBackbufferSize(p_context, rtWidth, rtHeight)) return;

        // Collect SRV descs first so we can decide relevance before doing any GPU readback work.
        ID3D11ShaderResourceView* srvs[4] = {};
        D3D11_TEXTURE2D_DESC srvDescs[4] = {};
        bool srvHasDesc[4] = {};
        p_context->PSGetShaderResources(0, 4, srvs);
        bool anyScaled = false;
        for (UINT i = 0; i < 4; i++) {
            if (srvs[i] == nullptr) continue;
            ID3D11Resource* pRes = nullptr;
            srvs[i]->GetResource(&pRes);
            if (pRes != nullptr) {
                ID3D11Texture2D* pTex = nullptr;
                if (SUCCEEDED(pRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTex)) && pTex != nullptr) {
                    pTex->GetDesc(&srvDescs[i]);
                    srvHasDesc[i] = true;
                    // >10% off in height (or width) from the backbuffer is treated as "scaled" —
                    // catches a half-height player texture (490 vs 979), not a 1-pixel
                    // typeless-vs-typed padding difference (980 vs 979).
                    if (std::abs((int)srvDescs[i].Height - (int)rtHeight) > (int)(rtHeight / 10) ||
                        std::abs((int)srvDescs[i].Width  - (int)rtWidth)  > (int)(rtWidth  / 10)) {
                        anyScaled = true;
                    }
                    pTex->Release();
                }
                pRes->Release();
            }
        }

        if (!anyScaled) {
            for (UINT i = 0; i < 4; i++) if (srvs[i] != nullptr) srvs[i]->Release();
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (now - lastLogTime < 1000) {
            for (UINT i = 0; i < 4; i++) if (srvs[i] != nullptr) srvs[i]->Release();
            return;
        }
        lastLogTime = now;

        LOG_INFO("D3D11 [Halo2]: === COMPOSITE CANDIDATE ({}) === count={} startLocation={} rt={}x{}",
                  drawKind, count, startLocation, rtWidth, rtHeight);

        for (UINT i = 0; i < 4; i++) {
            if (srvs[i] == nullptr) continue;
            if (srvHasDesc[i]) {
                LOG_INFO("D3D11 [Halo2]: composite PS SRV[{}] texture {}x{} format={} sampleCount={}",
                          i, srvDescs[i].Width, srvDescs[i].Height, (int)srvDescs[i].Format, srvDescs[i].SampleDesc.Count);
            }
            srvs[i]->Release();
        }

        ID3D11Buffer* pVB = nullptr;
        UINT stride = 0, vbOffset = 0;
        p_context->IAGetVertexBuffers(0, 1, &pVB, &stride, &vbOffset);
        LOG_INFO("D3D11 [Halo2]: composite vertex buffer stride={} offset={}", stride, vbOffset);
        LogBufferFloatSample(pVB, vbOffset, 256, "vertex buffer");
        if (pVB != nullptr) pVB->Release();

        ID3D11Buffer* cbs[4] = {};
        p_context->PSGetConstantBuffers(0, 4, cbs);
        for (UINT i = 0; i < 4; i++) {
            if (cbs[i] == nullptr) continue;
            char label[32];
            snprintf(label, sizeof(label), "PS constant buffer[%u]", i);
            LogBufferFloatSample(cbs[i], 0, 64, label);
            cbs[i]->Release();
        }

        ID3D11Buffer* vcbs[4] = {};
        p_context->VSGetConstantBuffers(0, 4, vcbs);
        for (UINT i = 0; i < 4; i++) {
            if (vcbs[i] == nullptr) continue;
            char label[32];
            snprintf(label, sizeof(label), "VS constant buffer[%u]", i);
            LogBufferFloatSample(vcbs[i], 0, 64, label);
            vcbs[i]->Release();
        }
    }

    // Root cause (confirmed 2026-09-15 via direct GPU vertex-buffer capture, AA-on vs AA-off,
    // 2-player splitscreen): Halo 2's own per-player composite draw (the pass that blits each
    // player's half-height render into its slot of the backbuffer) writes the correct destination
    // quad — (0,0)-(rtWidth, rtHeight/2) — when AA is on, but writes a quad reaching almost the
    // full backbuffer height instead when AA is off. The composite's shader constant buffers are
    // otherwise near-identical between the two states, so this is a CPU-side vertex-generation bug
    // in the game itself (likely a fallback path taken when there's no MSAA target to resolve),
    // not something fixable via a shader constant. Confirmed via halo2.dll's own D3D11 calls, not
    // a guess: see the AA-on/AA-off capture comparison this fix was built from.
    //
    // Fixed here by overriding the VIEWPORT for the correct slot, computed fresh on every
    // qualifying call from the CURRENTLY bound backbuffer size and AlphaRing's own configured
    // player_count — never a hardcoded resolution or player count, so this holds at any screen
    // size and for any number of horizontally-split players.
    //
    // First attempt used a scissor rect instead of a viewport override, reasoning that a scissor
    // purely crops without touching the vertex-to-pixel mapping. That was wrong in practice
    // (confirmed 2026-09-15 by live testing): the rect ended up correctly sized and positioned,
    // but the content inside it was still stretched — a scissor only discards fragments outside
    // the rect, it doesn't change WHERE inside that rect each source pixel lands, and the
    // underlying bug means the game's own geometry already spans (close to) the full clip-space
    // range for this quad, meant to fill the WHOLE backbuffer height, not just this player's
    // slice. Overriding the viewport is the actual fix: the D3D11 viewport transform rescales
    // whatever clip-space range a primitive occupies to fill the CURRENT viewport's rectangle
    // (that's true regardless of the shader's internal math, since it happens after the vertex
    // shader, purely as part of the fixed-function NDC-to-pixel mapping) — so pointing the
    // viewport at just this player's slice makes the pipeline naturally redistribute the whole
    // (buggy, oversized) mapping to correctly fill exactly that slice, instead of overflowing
    // past it. A viewport also clips to its own bounds, so no separate scissor rect is needed.
    static int s_compositeSlotThisFrame = 0;

    void ResetCompositeFrameCounter() {
        s_compositeSlotThisFrame = 0;
    }

    // RAII: on construction, if this draw call looks like a genuine per-player composite (targets
    // the backbuffer, tiny vertex/index count, and — the specific signal that distinguishes it
    // from an unrelated same-shaped backbuffer blit like a dynamic-resolution upscale pass — binds
    // an input texture whose height is close to rtHeight/player_count), clips it to the correct
    // horizontal slot for as long as this guard is alive. Composites are assumed to be issued in
    // player order each frame (slot 0, then slot 1, ...), tracked via s_compositeSlotThisFrame and
    // reset once per frame from the Present hook — this matches every capture taken so far, but if
    // a future capture shows player composites interleaved with unrelated backbuffer draws in a
    // different order, this assumption would need revisiting.
    struct CompositeSlotClipGuard {
        ID3D11DeviceContext* p_context;
        bool active = false;
        D3D11_VIEWPORT prevViewport = {};
        UINT prevNumViewports = 0;

        CompositeSlotClipGuard(ID3D11DeviceContext* p_ctx, UINT count) : p_context(p_ctx) {
            auto p_splitscreen = AlphaRing::Global::MCC::Splitscreen();
            if (p_splitscreen == nullptr || p_splitscreen->player_count <= 1) return;
            if (count == 0 || count > 6) return;

            UINT rtWidth = 0, rtHeight = 0;
            if (!GetBoundBackbufferSize(p_context, rtWidth, rtHeight)) return;

            UINT sliceHeight = rtHeight / (UINT)p_splitscreen->player_count;
            if (sliceHeight == 0) return;

            ID3D11ShaderResourceView* pSrv0 = nullptr;
            p_context->PSGetShaderResources(0, 1, &pSrv0);
            if (pSrv0 == nullptr) return;

            bool matches = false;
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
            pSrv0->Release();

            if (!matches) return;

            int slot = s_compositeSlotThisFrame % p_splitscreen->player_count;
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

        // Deliberately NOT gated on Hook::IsWine() (unlike the earlier revisions of this file):
        // the whole point right now is a like-for-like comparison of this same log against a
        // Windows capture, to see whether Windows ever takes the same 1280x360/high-draw-rate
        // path Linux gets stuck in, or never goes near it. Cheap (once/sec), so safe to leave on
        // for both platforms; re-add an IsWine() gate once this question is answered.
        {
            static CGameGlobal::eGame last_logged_game = (CGameGlobal::eGame)-1;
            auto pGameGlobal = GameGlobal();
            if (pGameGlobal != nullptr && pGameGlobal->current_game != last_logged_game) {
                last_logged_game = pGameGlobal->current_game;
                LOG_INFO("D3D11: active game changed to {}", GameName(last_logged_game));
            }

            if (InHalo2()) {
                LogCompositeDrawCandidate(p_context, "DrawIndexed", IndexCount, StartIndexLocation);

                static ULONGLONG window_start = 0;
                static UINT draw_count = 0;

                ULONGLONG now = GetTickCount64();
                if (window_start == 0) window_start = now;
                draw_count++;

                if (now - window_start >= 1000) {
                    LOG_INFO("D3D11 [Halo2]: {} DrawIndexed calls/sec", draw_count);
                    LogBoundRenderTarget(p_context);
                    draw_count = 0;
                    window_start = now;
                }
            }
        }

        // Guard's own count==0 early-out doubles as the "not Halo2" gate — this fix is specific
        // to the confirmed Halo2 composite bug and shouldn't touch other games' rendering.
        HRESULT result;
        {
            CompositeSlotClipGuard clip(p_context, InHalo2() ? IndexCount : 0);
            result = ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);
        }
        return result;
    }

    // Non-indexed Draw — vtable offset 0x68 (functions[74]), confirmed both from AlphaRing's own
    // functions[] table layout (src/render/d3d11/D3d11.cpp) and independently via a Ghidra scan
    // of halo2.dll's real ID3D11DeviceContext call sites. A full-screen composite quad is exactly
    // as likely to be issued via Draw (non-indexed) as via DrawIndexed, so both are instrumented.
    DefDetourFunction(HRESULT, __stdcall, Draw, ID3D11DeviceContext* p_context,
                      const UINT VertexCount, const UINT StartVertexLocation) {
        bool inHalo2 = InHalo2();
        if (inHalo2) {
            LogCompositeDrawCandidate(p_context, "Draw", VertexCount, StartVertexLocation);
        }

        HRESULT result;
        {
            CompositeSlotClipGuard clip(p_context, inHalo2 ? VertexCount : 0);
            result = ppOriginal_Draw(p_context, VertexCount, StartVertexLocation);
        }
        return result;
    }

    bool CreateHook() {
        return Hook::Detour({
            {GetFunction(73), DrawIndexed, (void **) &ppOriginal_DrawIndexed},
            {GetFunction(74), Draw, (void **) &ppOriginal_Draw},
        });
    }
}
