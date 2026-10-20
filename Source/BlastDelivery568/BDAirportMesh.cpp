#include "BDAirportMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* BDAirportMesh::Build(UObject* Outer,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,bool bConvexCollision)
{
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description); Attributes.Register();
    auto Positions=Attributes.GetVertexPositions();
    auto Normals=Attributes.GetVertexInstanceNormals();
    auto Tangents=Attributes.GetVertexInstanceTangents();
    auto Signs=Attributes.GetVertexInstanceBinormalSigns();
    auto UVs=Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
    auto Colors=Attributes.GetVertexInstanceColors();
    TArray<FVector> SmoothNormals; SmoothNormals.Init(FVector::ZeroVector,Vertices.Num());
    for (int32 I=0;I<Triangles.Num();I+=3)
    {
        const int32 A=Triangles[I],B=Triangles[I+1],C=Triangles[I+2];
        const FVector N=FVector::CrossProduct(Vertices[B]-Vertices[A],Vertices[C]-Vertices[A]);
        SmoothNormals[A]+=N; SmoothNormals[B]+=N; SmoothNormals[C]+=N;
    }
    TArray<FVertexID> VertexIDs;
    for (const FVector& P:Vertices) { FVertexID ID=Description.CreateVertex(); Positions[ID]=FVector3f(P); VertexIDs.Add(ID); }
    const FPolygonGroupID Group=Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("AirportSurface");
    for (int32 I=0;I<Triangles.Num();I+=3)
    {
        TArray<FVertexInstanceID> Face;
        for (int32 J=0;J<3;++J)
        {
            const int32 Index=Triangles[I+J];
            const FVertexInstanceID ID=Description.CreateVertexInstance(VertexIDs[Index]);
            const FVector N=SmoothNormals[Index].GetSafeNormal();
            const FVector Axis=FMath::Abs(N.Z)<.9f ? FVector::UpVector : FVector::ForwardVector;
            Normals[ID]=FVector3f(N); Tangents[ID]=FVector3f(FVector::CrossProduct(Axis,N).GetSafeNormal());
            Signs[ID]=1; Colors[ID]=FVector4f(1,1,1,1); UVs.Set(ID,0,FVector2f(Vertices[Index].X/100,Vertices[Index].Y/100));
            Face.Add(ID);
        }
        Description.CreateTriangle(Group,Face);
    }
    auto* Mesh=NewObject<UStaticMesh>(Outer);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr,TEXT("AirportSurface")));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild=true; Params.bMarkPackageDirty=false; Params.bCommitMeshDescription=false; Params.bAllowCpuAccess=true;
    Mesh->BuildFromMeshDescriptions({&Description},Params);
    if (bConvexCollision)
    {
        Mesh->CreateBodySetup();
        auto* Body=Mesh->GetBodySetup();
        Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        FKConvexElem Convex; Convex.VertexData=Vertices; Convex.UpdateElemBox();
        Body->AggGeom.ConvexElems.Add(MoveTemp(Convex));
        Body->CreatePhysicsMeshes();
    }
    return Mesh;
}
