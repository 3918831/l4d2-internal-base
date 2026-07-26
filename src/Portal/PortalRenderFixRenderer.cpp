#include "PortalRenderFixRenderer.h"

#include "PortalRenderFixGeometry.h"
#include "PortalRenderFixMeshWriter.h"

#include "../SDK/SDK.h"
#include "../SDK/L4D2/Interfaces/MatRenderContext.h"
#include "../Util/Logger/PortalFileLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    enum PortalMaterialPrimitiveType : int
    {
        PortalMaterialPoints = 0,
        PortalMaterialLines,
        PortalMaterialTriangles,
        PortalMaterialTriangleStrip,
    };

    using PortalVertexFormat = std::uint64_t;

    struct PortalVertexDesc
    {
        int vertexSizePosition;
        int vertexSizeBoneWeight;
        int vertexSizeBoneMatrixIndex;
        int vertexSizeNormal;
        int vertexSizeColor;
        int vertexSizeSpecular;
        int vertexSizeTexCoord[8];
        int vertexSizeTangentS;
        int vertexSizeTangentT;
        int vertexSizeWrinkle;
        int vertexSizeUserData;
        int actualVertexSize;
        int compressionType;
        int numBoneWeights;
        float* position;
        float* boneWeight;
        unsigned char* boneMatrixIndex;
        float* normal;
        unsigned char* color;
        unsigned char* specular;
        float* texCoord[8];
        float* tangentS;
        float* tangentT;
        float* wrinkle;
        float* userData;
        int firstVertex;
        unsigned int offset;
    };

    struct PortalIndexDesc
    {
        unsigned short* indices;
        unsigned int offset;
        unsigned int firstIndex;
        unsigned char indexSize;
    };

    struct PortalMeshDesc : PortalVertexDesc, PortalIndexDesc
    {
    };

#if defined(_M_IX86)
    static_assert(
        sizeof(PortalVertexDesc) == 164,
        "PortalVertexDesc must match the Source Windows x86 ABI.");
    static_assert(
        sizeof(PortalIndexDesc) == 16,
        "PortalIndexDesc must match the Source Windows x86 ABI.");
    static_assert(
        sizeof(PortalMeshDesc) == 180,
        "PortalMeshDesc must match the Source Windows x86 ABI.");
#endif

    struct PortalPrimList
    {
        int firstIndex;
        int indexCount;
    };

    class PortalVertexBuffer
    {
    public:
        virtual ~PortalVertexBuffer() {}
        virtual int VertexCount() const = 0;
        virtual PortalVertexFormat GetVertexFormat() const = 0;
        virtual bool IsDynamic() const = 0;
        virtual void BeginCastBuffer(PortalVertexFormat format) = 0;
        virtual void EndCastBuffer() = 0;
        virtual int GetRoomRemaining() const = 0;
        virtual bool Lock(int vertexCount, bool append, PortalVertexDesc& desc) = 0;
        virtual void Unlock(int vertexCount, PortalVertexDesc& desc) = 0;
        virtual void Spew(int vertexCount, const PortalVertexDesc& desc) = 0;
        virtual void ValidateData(int vertexCount, const PortalVertexDesc& desc) = 0;
    };

    class PortalIndexBuffer
    {
    public:
        virtual ~PortalIndexBuffer() {}
        virtual int IndexCount() const = 0;
        virtual int IndexFormat() const = 0;
        virtual bool IsDynamic() const = 0;
        virtual void BeginCastBuffer(int format) = 0;
        virtual void EndCastBuffer() = 0;
        virtual int GetRoomRemaining() const = 0;
        virtual bool Lock(int indexCount, bool append, PortalIndexDesc& desc) = 0;
        virtual void Unlock(int indexCount, PortalIndexDesc& desc) = 0;
        virtual bool ModifyBegin(
            bool readOnly,
            int firstIndex,
            int indexCount,
            PortalIndexDesc& desc) = 0;
        virtual void ModifyEnd(PortalIndexDesc& desc) = 0;
        virtual void Spew(int indexCount, const PortalIndexDesc& desc) = 0;
        virtual void ValidateData(int indexCount, const PortalIndexDesc& desc) = 0;
    };

    class PortalMesh : public PortalVertexBuffer, public PortalIndexBuffer
    {
    public:
        virtual void SetPrimitiveType(PortalMaterialPrimitiveType type) = 0;
        virtual void Draw(int firstIndex = -1, int indexCount = 0) = 0;
        virtual void SetColorMesh(PortalMesh* colorMesh, int vertexOffset) = 0;
        virtual void Draw(PortalPrimList* lists, int listCount) = 0;
        virtual void CopyToMeshBuilder(
            int startVertex,
            int vertexCount,
            int startIndex,
            int indexCount,
            int indexOffset,
            void* builder) = 0;
        virtual void Spew(
            int vertexCount,
            int indexCount,
            const PortalMeshDesc& desc) = 0;
        virtual void ValidateData(
            int vertexCount,
            int indexCount,
            const PortalMeshDesc& desc) = 0;
        virtual void LockMesh(
            int vertexCount,
            int indexCount,
            PortalMeshDesc& desc) = 0;
        virtual void ModifyBegin(
            int firstVertex,
            int vertexCount,
            int firstIndex,
            int indexCount,
            PortalMeshDesc& desc) = 0;
        virtual void ModifyEnd(PortalMeshDesc& desc) = 0;
        virtual void UnlockMesh(
            int vertexCount,
            int indexCount,
            PortalMeshDesc& desc) = 0;
        virtual void ModifyBeginEx(
            bool readOnly,
            int firstVertex,
            int vertexCount,
            int firstIndex,
            int indexCount,
            PortalMeshDesc& desc) = 0;
        virtual void SetFlexMesh(PortalMesh* mesh, int vertexOffset) = 0;
        virtual void DisableFlexMesh() = 0;
        virtual void MarkAsDrawn() = 0;
        virtual unsigned int ComputeMemoryUsed() = 0;
    };

    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    Vector TransformPointToNdc(
        const VMatrix& view,
        const VMatrix& projection,
        const Vector& point,
        bool& valid)
    {
        float viewPoint[4] = {};
        float clipPoint[4] = {};
        const float worldPoint[4] = { point.x, point.y, point.z, 1.0f };

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
                viewPoint[row] += view.m[row][column] * worldPoint[column];
        }

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
                clipPoint[row] += projection.m[row][column] * viewPoint[column];
        }

        if (!std::isfinite(clipPoint[3]) || clipPoint[3] <= 1.0e-5f)
        {
            valid = false;
            return Vector();
        }

        const float inverseW = 1.0f / clipPoint[3];
        const Vector result(
            clipPoint[0] * inverseW,
            clipPoint[1] * inverseW,
            0.00001f);
        valid = IsFiniteVector(result);
        return result;
    }

    bool ClipNdcToPlane(
        const PortalRenderFixGeometry::Polygon& input,
        const Vector& normal,
        float distance,
        PortalRenderFixGeometry::Polygon& output)
    {
        output.count = 0;
        if (input.count < 3)
            return false;

        for (std::size_t i = 0; i < input.count; ++i)
        {
            const Vector& current = input.vertices[i];
            const Vector& next = input.vertices[(i + 1) % input.count];
            const float currentDistance = current.Dot(normal) - distance;
            const float nextDistance = next.Dot(normal) - distance;
            const bool currentInside = currentDistance >= -0.0001f;
            const bool nextInside = nextDistance >= -0.0001f;

            if (currentInside)
            {
                if (output.count >= output.vertices.size())
                    return false;
                output.vertices[output.count++] = current;
            }

            if (currentInside == nextInside)
                continue;

            const float denominator = currentDistance - nextDistance;
            if (std::fabs(denominator) < 1.0e-6f)
                continue;

            const float fraction = currentDistance / denominator;
            Vector intersection = current + (next - current) * fraction;
            intersection.z = 0.00001f;
            if (!IsFiniteVector(intersection)
                || output.count >= output.vertices.size())
            {
                return false;
            }
            output.vertices[output.count++] = intersection;
        }

        return output.count >= 3;
    }

    bool ProjectAndClipToNdc(
        IMatRenderContext* renderContext,
        const PortalRenderFixGeometry::Polygon& worldPolygon,
        PortalRenderFixGeometry::Polygon& ndcPolygon)
    {
        VMatrix view;
        VMatrix projection;
        renderContext->GetMatrix(MATERIAL_VIEW, &view);
        renderContext->GetMatrix(MATERIAL_PROJECTION, &projection);

        PortalRenderFixGeometry::Polygon current;
        current.count = worldPolygon.count;
        for (std::size_t i = 0; i < worldPolygon.count; ++i)
        {
            bool valid = false;
            current.vertices[i] =
                TransformPointToNdc(view, projection, worldPolygon.vertices[i], valid);
            if (!valid)
                return false;
        }

        static const std::array<Vector, 4> kNormals = {
            Vector(1.0f, 0.0f, 0.0f),
            Vector(-1.0f, 0.0f, 0.0f),
            Vector(0.0f, 1.0f, 0.0f),
            Vector(0.0f, -1.0f, 0.0f),
        };
        static constexpr std::array<float, 4> kDistances = {
            -1.0f, -1.0f, -1.0f, -1.0f,
        };

        PortalRenderFixGeometry::Polygon clipped;
        for (std::size_t i = 0; i < kNormals.size(); ++i)
        {
            if (!ClipNdcToPlane(current, kNormals[i], kDistances[i], clipped))
                return false;
            current = clipped;
        }

        ndcPolygon = current;
        return ndcPolygon.count >= 3;
    }

    template <typename T>
    T* OffsetPointer(T* pointer, std::size_t index, int stride)
    {
        if (!pointer || stride <= 0)
            return nullptr;

        return reinterpret_cast<T*>(
            reinterpret_cast<unsigned char*>(pointer) + index * stride);
    }

    struct MeshDrawDiagnostics
    {
        int firstVertex = 0;
        unsigned int firstIndex = 0;
        unsigned char indexSize = 0;
        PortalRenderFixMeshWriter::WriteResult indexWrite;
    };

    bool DrawNdcPolygon(
        IMatRenderContext* renderContext,
        IMaterial* stencilMaterial,
        const PortalRenderFixGeometry::Polygon& polygon,
        MeshDrawDiagnostics& diagnostics)
    {
        diagnostics = MeshDrawDiagnostics();
        if (polygon.count < 3 || polygon.count > 32768)
            return false;

        renderContext->Bind(stencilMaterial);
        PortalMesh* mesh = reinterpret_cast<PortalMesh*>(
            renderContext->GetDynamicMesh(
                true,
                nullptr,
                nullptr,
                stencilMaterial));
        if (!mesh)
            return false;

        PortalMeshDesc desc{};
        const int count = static_cast<int>(polygon.count);
        mesh->SetPrimitiveType(PortalMaterialTriangleStrip);
        mesh->LockMesh(count, count, desc);

        diagnostics.firstVertex = desc.firstVertex;
        diagnostics.firstIndex = desc.firstIndex;
        diagnostics.indexSize = desc.indexSize;

        if (!desc.position || desc.vertexSizePosition <= 0
            || !desc.indices)
        {
            mesh->UnlockMesh(0, 0, desc);
            return false;
        }

        int forward = 0;
        int reverse = count - 1;
        int stripIndex = 0;
        while (forward <= reverse)
        {
            const int sourceIndices[2] = { forward++, reverse-- };
            const int sourceCount = forward - 1 <= reverse + 1 ? 2 : 1;
            for (int source = 0; source < sourceCount; ++source)
            {
                if (stripIndex >= count)
                    break;

                const Vector& point = polygon.vertices[sourceIndices[source]];
                float* position =
                    OffsetPointer(desc.position, stripIndex, desc.vertexSizePosition);
                if (!position)
                {
                    mesh->UnlockMesh(stripIndex, stripIndex, desc);
                    return false;
                }

                position[0] = point.x;
                position[1] = point.y;
                position[2] = point.z;

                float* texCoord =
                    OffsetPointer(desc.texCoord[0], stripIndex, desc.vertexSizeTexCoord[0]);
                if (texCoord)
                {
                    texCoord[0] = 0.0f;
                    texCoord[1] = 0.0f;
                }

                ++stripIndex;
            }
        }

        if (!PortalRenderFixMeshWriter::WriteSequentialStripIndices(
                desc.indices,
                static_cast<std::size_t>(count),
                stripIndex,
                desc.firstVertex,
                desc.indexSize,
                diagnostics.indexWrite))
        {
            mesh->UnlockMesh(0, 0, desc);
            return false;
        }

        mesh->UnlockMesh(stripIndex, stripIndex, desc);
        if (stripIndex < 3)
            return false;

        mesh->Draw();
        return true;
    }

    bool DrawWithIdentityMatrices(
        IMatRenderContext* renderContext,
        IMaterial* stencilMaterial,
        const PortalRenderFixGeometry::Polygon& polygon,
        MeshDrawDiagnostics& diagnostics)
    {
        const bool clippingEnabled = renderContext->EnableClipping(false);

        renderContext->MatrixMode(MATERIAL_MODEL);
        renderContext->PushMatrix();
        renderContext->LoadIdentity();
        renderContext->MatrixMode(MATERIAL_VIEW);
        renderContext->PushMatrix();
        renderContext->LoadIdentity();
        renderContext->MatrixMode(MATERIAL_PROJECTION);
        renderContext->PushMatrix();
        renderContext->LoadIdentity();

        const bool drawn =
            DrawNdcPolygon(
                renderContext,
                stencilMaterial,
                polygon,
                diagnostics);

        renderContext->MatrixMode(MATERIAL_MODEL);
        renderContext->PopMatrix();
        renderContext->MatrixMode(MATERIAL_VIEW);
        renderContext->PopMatrix();
        renderContext->MatrixMode(MATERIAL_PROJECTION);
        renderContext->PopMatrix();
        renderContext->EnableClipping(clippingEnabled);
        return drawn;
    }

    void LogApplied(
        const char* portalName,
        std::size_t vertexCount,
        float eyeDepth,
        float zNear,
        const MeshDrawDiagnostics& diagnostics)
    {
        static float lastBlueTime = -1000.0f;
        static float lastOrangeTime = -1000.0f;
        float& lastTime =
            portalName && std::strcmp(portalName, "Blue") == 0
            ? lastBlueTime
            : lastOrangeTime;
        const float currentTime =
            I::EngineClient ? I::EngineClient->OBSOLETE_Time() : 0.0f;
        if (currentTime - lastTime < 1.0f)
            return;

        lastTime = currentTime;
        U::PortalFileLog::WriteFormat(
            "[PortalRenderFix] applied=true portal=%s time=%.3f vertices=%zu eyeDepth=%.4f zNear=%.3f firstVertex=%d firstIndex=%u indexSize=%u firstIndexValue=%u lastIndexValue=%u proxyNdcZ=0.00001 mode=StencilProxyOnly.\n",
            portalName ? portalName : "Unknown",
            currentTime,
            vertexCount,
            eyeDepth,
            zNear,
            diagnostics.firstVertex,
            diagnostics.firstIndex,
            static_cast<unsigned int>(diagnostics.indexSize),
            static_cast<unsigned int>(
                diagnostics.indexWrite.firstIndexValue),
            static_cast<unsigned int>(
                diagnostics.indexWrite.lastIndexValue));
    }
}

PortalRenderFixRenderer::DrawResult
PortalRenderFixRenderer::DrawMainViewStencilProxy(
    IMatRenderContext* renderContext,
    IMaterial* stencilMaterial,
    const CViewSetup& view,
    const Vector& portalOrigin,
    const QAngle& portalAngles,
    float portalScale,
    const char* portalName)
{
    DrawResult result;
    if (!renderContext
        || !stencilMaterial
        || !std::isfinite(portalScale)
        || portalScale <= 0.01f)
    {
        return result;
    }

    PortalRenderFixGeometry::BuildInput input;
    input.cameraOrigin = view.origin;
    U::Math.AngleVectors(
        view.angles,
        &input.cameraForward,
        &input.cameraRight,
        &input.cameraUp);
    input.zNear = view.zNear;
    input.portalOrigin = portalOrigin;
    U::Math.AngleVectors(
        portalAngles,
        &input.portalForward,
        &input.portalRight,
        &input.portalUp);
    input.halfWidth = 32.0f * std::clamp(portalScale, 0.01f, 1.0f);
    input.halfHeight = 54.0f * std::clamp(portalScale, 0.01f, 1.0f);
    result.eyeDepth =
        (view.origin - portalOrigin).Dot(input.portalForward);

    PortalRenderFixGeometry::Polygon worldPolygon;
    if (!PortalRenderFixGeometry::BuildNearPlaneAperturePolygon(
            input,
            worldPolygon))
    {
        return result;
    }

    PortalRenderFixGeometry::Polygon ndcPolygon;
    if (!ProjectAndClipToNdc(renderContext, worldPolygon, ndcPolygon))
        return result;

    MeshDrawDiagnostics diagnostics;
    if (!DrawWithIdentityMatrices(
            renderContext,
            stencilMaterial,
            ndcPolygon,
            diagnostics))
    {
        return result;
    }

    result.applied = true;
    result.vertexCount = ndcPolygon.count;
    LogApplied(
        portalName,
        result.vertexCount,
        result.eyeDepth,
        view.zNear,
        diagnostics);
    return result;
}
