#include "collision.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <algorithm>

// ==========================================
// محرك الفيزياء الاحترافي (نظام تجاهل السلالم Hover - C++ Native)
// متطابق 100% مع البنية المطلوبة لدوال اللعبة
// ==========================================

namespace GameCollision {

    // ==========================================
    // هياكل البيانات والدوال الرياضية المساعدة
    // ==========================================
    
    struct Triangle3D {
        Vector3 a, b, c;
        
        bool operator==(const Triangle3D& other) const {
            return Vector3Equals(a, other.a) && Vector3Equals(b, other.b) && Vector3Equals(c, other.c);
        }
    };

    struct IntersectResult {
        bool hit;
        Vector3 normal;
        Vector3 point;
        float depth;
    };

    // استخراج العمودي (Normal) للمثلث
    Vector3 GetTriangleNormal(Triangle3D t) {
        Vector3 cb = Vector3Subtract(t.c, t.b);
        Vector3 ab = Vector3Subtract(t.a, t.b);
        return Vector3Normalize(Vector3CrossProduct(cb, ab));
    }

    // إسقاط نقطة على مستوى المثلث (Plane Projection)
    Vector3 ProjectPointOnPlane(Vector3 point, Vector3 planeNormal, Vector3 planePoint) {
        float dist = Vector3DotProduct(Vector3Subtract(point, planePoint), planeNormal);
        return Vector3Subtract(point, Vector3Scale(planeNormal, dist));
    }

    // التحقق مما إذا كانت النقطة داخل المثلث (Barycentric Coordinates)
    bool PointInTriangle(Vector3 p, Triangle3D t) {
        Vector3 v0 = Vector3Subtract(t.c, t.a);
        Vector3 v1 = Vector3Subtract(t.b, t.a);
        Vector3 v2 = Vector3Subtract(p, t.a);
        float dot00 = Vector3DotProduct(v0, v0);
        float dot01 = Vector3DotProduct(v0, v1);
        float dot02 = Vector3DotProduct(v0, v2);
        float dot11 = Vector3DotProduct(v1, v1);
        float dot12 = Vector3DotProduct(v1, v2);
        float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
        return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
    }

    // أقرب نقطة على خط
    Vector3 ClosestPointOnSegment(Vector3 p, Vector3 a, Vector3 b) {
        Vector3 ab = Vector3Subtract(b, a);
        float t = Vector3DotProduct(Vector3Subtract(p, a), ab) / Vector3LengthSqr(ab);
        t = Clamp(t, 0.0f, 1.0f);
        return Vector3Add(a, Vector3Scale(ab, t));
    }

    // ==========================================
    // فئة الكبسولة (Capsule) لحسابات التصادم الداخلية
    // ==========================================
    struct CapsuleInternal {
        Vector3 start;
        Vector3 end;
        float radius;

        Vector3 GetCenter() const {
            return Vector3Scale(Vector3Add(start, end), 0.5f);
        }

        void Translate(Vector3 v) {
            start = Vector3Add(start, v);
            end = Vector3Add(end, v);
        }

        bool CheckAABBAxis(float p1x, float p1y, float p2x, float p2y, float minx, float maxx, float miny, float maxy, float rad) const {
            return ((minx - p1x < rad || minx - p2x < rad) &&
                    (p1x - maxx < rad || p2x - maxx < rad) &&
                    (miny - p1y < rad || miny - p2y < rad) &&
                    (p1y - maxy < rad || p2y - maxy < rad));
        }

        bool IntersectsBox(BoundingBox box) const {
            return (CheckAABBAxis(start.x, start.y, end.x, end.y, box.min.x, box.max.x, box.min.y, box.max.y, radius) &&
                    CheckAABBAxis(start.x, start.z, end.x, end.z, box.min.x, box.max.x, box.min.z, box.max.z, radius) &&
                    CheckAABBAxis(start.y, start.z, end.y, end.z, box.min.y, box.max.y, box.min.z, box.max.z, radius));
        }

        void LineLineMinimumPoints(Vector3 l1_start, Vector3 l1_end, Vector3 l2_start, Vector3 l2_end, Vector3& pt1, Vector3& pt2) const {
            Vector3 r = Vector3Subtract(l1_end, l1_start);
            Vector3 s = Vector3Subtract(l2_end, l2_start);
            Vector3 w = Vector3Subtract(l2_start, l1_start);
            float a = Vector3DotProduct(r, s);
            float b = Vector3DotProduct(r, r);
            float c = Vector3DotProduct(s, s);
            float d = Vector3DotProduct(s, w);
            float e = Vector3DotProduct(r, w);
            float denominator = b * c - a * a;
            float t1, t2;
            
            if (denominator < 1e-8f) {
                t1 = 0.0f;
                t2 = d / c;
            } else {
                t1 = (a * d - c * e) / denominator;
                t2 = (a * e - b * d) / denominator;
            }
            
            t1 = Clamp(t1, 0.0f, 1.0f);
            t2 = Clamp(t2, 0.0f, 1.0f);
            
            pt1 = Vector3Add(l1_start, Vector3Scale(r, t1));
            pt2 = Vector3Add(l2_start, Vector3Scale(s, t2));
        }
    };

    // ==========================================
    // الشجرة الثمانية (Octree)
    // ==========================================
    struct Octree {
        std::vector<Triangle3D> triangles;
        BoundingBox box;
        BoundingBox bounds;
        std::vector<Octree*> subTrees;
        bool hasBounds = false;

        Octree() {
            box = { {0,0,0}, {0,0,0} };
            bounds = { {INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY} };
        }
        Octree(BoundingBox b) : box(b) {
            bounds = { {INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY} };
        }
        ~Octree() { for (auto t : subTrees) delete t; }

        void AddTriangle(Triangle3D triangle) {
            hasBounds = true;
            bounds.min.x = fminf(bounds.min.x, fminf(fminf(triangle.a.x, triangle.b.x), triangle.c.x));
            bounds.min.y = fminf(bounds.min.y, fminf(fminf(triangle.a.y, triangle.b.y), triangle.c.y));
            bounds.min.z = fminf(bounds.min.z, fminf(fminf(triangle.a.z, triangle.b.z), triangle.c.z));
            
            bounds.max.x = fmaxf(bounds.max.x, fmaxf(fmaxf(triangle.a.x, triangle.b.x), triangle.c.x));
            bounds.max.y = fmaxf(bounds.max.y, fmaxf(fmaxf(triangle.a.y, triangle.b.y), triangle.c.y));
            bounds.max.z = fmaxf(bounds.max.z, fmaxf(fmaxf(triangle.a.z, triangle.b.z), triangle.c.z));
            
            triangles.push_back(triangle);
        }

        void CalcBox() {
            if (!hasBounds) return;
            box = bounds;
            box.min = Vector3Subtract(box.min, {0.01f, 0.01f, 0.01f});
            box.max = Vector3Add(box.max, {0.01f, 0.01f, 0.01f});
        }

        bool IntersectsTriangleAABB(BoundingBox b, Triangle3D t) {
            float minX = fminf(fminf(t.a.x, t.b.x), t.c.x);
            float minY = fminf(fminf(t.a.y, t.b.y), t.c.y);
            float minZ = fminf(fminf(t.a.z, t.b.z), t.c.z);
            float maxX = fmaxf(fmaxf(t.a.x, t.b.x), t.c.x);
            float maxY = fmaxf(fmaxf(t.a.y, t.b.y), t.c.y);
            float maxZ = fmaxf(fmaxf(t.a.z, t.b.z), t.c.z);
            
            if (maxX < b.min.x || minX > b.max.x) return false;
            if (maxY < b.min.y || minY > b.max.y) return false;
            if (maxZ < b.min.z || minZ > b.max.z) return false;
            return true; 
        }

        void Split(int level) {
            if (!hasBounds) return;
            Vector3 halfsize = Vector3Scale(Vector3Subtract(box.max, box.min), 0.5f);
            std::vector<Octree*> newSubTrees;

            for (int x = 0; x < 2; x++) {
                for (int y = 0; y < 2; y++) {
                    for (int z = 0; z < 2; z++) {
                        BoundingBox b;
                        Vector3 v = { (float)x, (float)y, (float)z };
                        b.min = Vector3Add(box.min, Vector3Multiply(v, halfsize));
                        b.max = Vector3Add(b.min, halfsize);
                        newSubTrees.push_back(new Octree(b));
                    }
                }
            }

            for (auto it = triangles.rbegin(); it != triangles.rend(); ++it) {
                Triangle3D t = *it;
                for (auto sub : newSubTrees) {
                    if (IntersectsTriangleAABB(sub->box, t)) sub->triangles.push_back(t);
                }
            }
            triangles.clear(); 

            for (auto sub : newSubTrees) {
                int len = sub->triangles.size();
                if (len > 8 && level < 16) sub->Split(level + 1);
                if (len != 0) subTrees.push_back(sub);
                else delete sub;
            }
        }

        void Build() {
            CalcBox();
            Split(0);
        }

        IntersectResult TriangleCapsuleIntersect(CapsuleInternal& capsule, Triangle3D t) {
            IntersectResult result = {false, {0,0,0}, {0,0,0}, 0.0f};
            Vector3 point;
            Vector3 normal = GetTriangleNormal(t);
            point = ProjectPointOnPlane(capsule.GetCenter(), normal, t.a);
            
            if (PointInTriangle(point, t)) {
                Vector3 closest = ClosestPointOnSegment(point, capsule.start, capsule.end);
                float distance = Vector3Distance(closest, point);
                if (distance < capsule.radius) {
                    result.hit = true; result.normal = normal; result.point = point; result.depth = capsule.radius - distance;
                    return result;
                }
            }
            
            Vector3 edges[3][2] = { {t.a, t.b}, {t.b, t.c}, {t.c, t.a} };
            float minDistance = INFINITY; Vector3 minPoint = {0}; Vector3 minNormal = {0};
            
            for (int i = 0; i < 3; i++) {
                Vector3 pt1, pt2;
                capsule.LineLineMinimumPoints(capsule.start, capsule.end, edges[i][0], edges[i][1], pt1, pt2);
                float dist = Vector3Distance(pt1, pt2);
                if (dist < capsule.radius && dist < minDistance) {
                    minDistance = dist; minPoint = pt2; minNormal = Vector3Normalize(Vector3Subtract(pt1, pt2));
                }
            }
            
            if (minDistance < capsule.radius) {
                result.hit = true; result.normal = minNormal; result.point = minPoint; result.depth = capsule.radius - minDistance;
            }
            return result;
        }

        void GetCapsuleTriangles(CapsuleInternal& capsule, std::vector<Triangle3D>& resultTriangles) {
            for (auto subTree : subTrees) {
                if (!capsule.IntersectsBox(subTree->box)) continue;
                if (!subTree->triangles.empty()) {
                    for (const auto& t : subTree->triangles) {
                        if (std::find(resultTriangles.begin(), resultTriangles.end(), t) == resultTriangles.end()) {
                            resultTriangles.push_back(t);
                        }
                    }
                } else {
                    subTree->GetCapsuleTriangles(capsule, resultTriangles);
                }
            }
        }

        IntersectResult DoCapsuleIntersect(CapsuleInternal& capsule, std::vector<Triangle3D>& _capsuleTriangles) {
            _capsuleTriangles.clear();
            if (triangles.size() > 0) _capsuleTriangles = triangles;
            else GetCapsuleTriangles(capsule, _capsuleTriangles);
            
            IntersectResult finalResult = {false, {0,0,0}, {0,0,0}, 0.0f};
            for (const auto& t : _capsuleTriangles) {
                IntersectResult result = TriangleCapsuleIntersect(capsule, t);
                if (result.hit) {
                    finalResult.hit = true;
                    capsule.Translate(Vector3Scale(result.normal, result.depth));
                    finalResult.normal = result.normal;
                    finalResult.depth = result.depth;
                }
            }
            return finalResult;
        }
    };

    // الكائن العام للـ Octree
    Octree octree;
    CapsuleInternal playerCapsule = { {0, 0.65f, 0}, {0, 1.4f, 0}, 0.25f };
    std::vector<Triangle3D> _capsuleTrianglesBuffer;

    // ==========================================
    // 1. الدالة الأساسية لإضافة المجسمات (API)
    // ==========================================
    void AddCollider(Model model, Matrix transform) {
        for (int i = 0; i < model.meshCount; i++) {
            Mesh mesh = model.meshes[i];
            float* vertices = (float*)mesh.vertices;
            unsigned short* indices = (unsigned short*)mesh.indices;
            
            if (indices != nullptr) {
                for (int j = 0; j < mesh.triangleCount * 3; j += 3) {
                    Vector3 vA = { vertices[indices[j]*3], vertices[indices[j]*3+1], vertices[indices[j]*3+2] };
                    Vector3 vB = { vertices[indices[j+1]*3], vertices[indices[j+1]*3+1], vertices[indices[j+1]*3+2] };
                    Vector3 vC = { vertices[indices[j+2]*3], vertices[indices[j+2]*3+1], vertices[indices[j+2]*3+2] };
                    vA = Vector3Transform(vA, transform); vB = Vector3Transform(vB, transform); vC = Vector3Transform(vC, transform);
                    octree.AddTriangle({vA, vB, vC});
                }
            } else {
                for (int j = 0; j < mesh.vertexCount * 3; j += 9) {
                    Vector3 vA = { vertices[j], vertices[j+1], vertices[j+2] };
                    Vector3 vB = { vertices[j+3], vertices[j+4], vertices[j+5] };
                    Vector3 vC = { vertices[j+6], vertices[j+7], vertices[j+8] };
                    vA = Vector3Transform(vA, transform); vB = Vector3Transform(vB, transform); vC = Vector3Transform(vC, transform);
                    octree.AddTriangle({vA, vB, vC});
                }
            }
        }
        octree.Build();
    }

    // ==========================================
    // 2. معالجة حركة اللاعب ضد الجدران (السلالم Hover)
    // ==========================================
    void ResolveMovement(Vector3& playerPos, Vector3& playerVelocity, float delta) {
        const int subSteps = 5; 
        float subDelta = delta / (float)subSteps;

        playerCapsule.start = Vector3Add(playerPos, {0.0f, 0.65f, 0.0f});
        playerCapsule.end = Vector3Add(playerPos, {0.0f, 1.4f, 0.0f});

        for (int i = 0; i < subSteps; i++) {
            playerCapsule.Translate(Vector3Scale(playerVelocity, subDelta));
            
            for(int c = 0; c < 3; c++) {
                IntersectResult result = octree.DoCapsuleIntersect(playerCapsule, _capsuleTrianglesBuffer);
                if (result.hit) {
                    playerCapsule.Translate(Vector3Scale(result.normal, result.depth));
                    float dot = Vector3DotProduct(result.normal, playerVelocity);
                    playerVelocity = Vector3Add(playerVelocity, Vector3Scale(result.normal, -dot));
                } else break;
            }
        }
        playerPos = Vector3Subtract(playerCapsule.start, {0.0f, 0.65f, 0.0f});
    }

    // ==========================================
    // 3. تصادم السيارات الخارجي (CapsuleIntersect API للسيارات)
    // ==========================================
    bool CapsuleIntersect(Vector3 start, Vector3 end, float radius, Vector3& outNormal, float& outDepth) {
        CapsuleInternal tempCapsule = { start, end, radius };
        std::vector<Triangle3D> tempBuffer;
        
        IntersectResult result = octree.DoCapsuleIntersect(tempCapsule, tempBuffer);
        if (result.hit) {
            outNormal = result.normal;
            outDepth = result.depth;
            return true;
        }
        return false;
    }

    // ==========================================
    // 4. إطلاق شعاع للكشف عن الأرضيات أو الطلقات
    // ==========================================
    bool Raycast(Vector3 origin, Vector3 dir, Vector3& outHitPoint) {
        // بما أن الـ Octree معقد لحسابات الـ Ray السريعة، نستخدم خط مستقيم يحاكي كبسولة رقيقة جداً وموجهة لأسفل
        CapsuleInternal rayCapsule = { origin, Vector3Add(origin, Vector3Scale(Vector3Normalize(dir), 200.0f)), 0.01f };
        std::vector<Triangle3D> tempBuffer;
        
        IntersectResult result = octree.DoCapsuleIntersect(rayCapsule, tempBuffer);
        if (result.hit) {
            outHitPoint = result.point;
            return true;
        }
        
        outHitPoint = origin; // نقطة وهمية في حالة عدم الإصابة
        return false; 
    }

}
