#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <limits>
#include <cassert>

#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <CGAL/Simple_cartesian.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/boost/graph/graph_traits_Polyhedron_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/Polyhedron_incremental_builder_3.h>
#include <CGAL/Polyhedral_mesh_domain_3.h>

#define ASSERT(expr) assert(expr)
#define EXIT_STATUS_INC __COUNTER__
#define STATIC_ASSERT(expr) static_assert(expr, #expr)

typedef CGAL::Simple_cartesian<double> Kernel;
typedef Kernel::Point_3 Point_3;
typedef CGAL::Polyhedron_3<Kernel> Polyhedron;
typedef CGAL::Polyhedral_mesh_domain_3<Polyhedron, Kernel> PolyhedralMeshDomain;
typedef CGAL::AABB_face_graph_triangle_primitive<Polyhedron> AABBPrimitive;
typedef CGAL::AABB_traits<Kernel, AABBPrimitive> AABBTraits;
typedef CGAL::AABB_tree<AABBTraits> AABBTree;

struct AABB
{
    aiVector3D min, max;
};

AABB computeAABB(const aiMesh* mesh)
{
    const float Inf = std::numeric_limits<float>::infinity();
    AABB ab;
    ab.min = aiVector3D(Inf, Inf, Inf);
    ab.max = -ab.min;

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D& v = mesh->mVertices[i];
        ab.min.x = std::min(v.x, ab.min.x);
        ab.min.y = std::min(v.y, ab.min.y);
        ab.min.z = std::min(v.z, ab.min.z);

        ab.max.x = std::max(v.x, ab.max.x);
        ab.max.y = std::max(v.y, ab.max.y);
        ab.max.z = std::max(v.z, ab.max.z);
    }

    return ab;
}

template <class HalfedgeDataStructure>
class CGALBuilder : public CGAL::Modifier_base<HalfedgeDataStructure>
{
public:
    CGALBuilder(const aiMesh* mesh): mesh(mesh) {}

    void operator()(HalfedgeDataStructure& hds)
    {
        typedef typename HalfedgeDataStructure::Vertex Vertex;
        typedef typename Vertex::Point Point;

        CGAL::Polyhedron_incremental_builder_3<HalfedgeDataStructure> B(hds, true);

        // Scale down the mesh to fit unit cube [0-1] (and a bit more). Center around (0.5, 0.5, 0.5).
        const AABB ab = computeAABB(mesh);
        const aiVector3D origin = (ab.max+ab.min) * 0.5f;
        const aiVector3D extents = ab.max-ab.min;
        const float scale = 0.8f / std::max(extents.x, std::max(extents.y, extents.z));

        B.begin_surface(mesh->mNumVertices, mesh->mNumFaces);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            aiVector3D v = mesh->mVertices[i];
            v = (v-origin)*scale + aiVector3D(0.5f, 0.5f, 0.5f);
            B.add_vertex(Point(v.x, v.y, v.z));
        }
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            B.begin_facet();
            B.add_vertex_to_facet(face.mIndices[0]);
            B.add_vertex_to_facet(face.mIndices[1]);
            B.add_vertex_to_facet(face.mIndices[2]);
            B.end_facet();
        }
        B.end_surface();
        ASSERT(B.error() == false);
    }

private:
    const aiMesh* mesh;
};

std::string getCmdOption(const std::vector<std::string>& args, const std::string& option)
{
    auto it = std::find(args.begin(), args.end(), option);
    if (it != args.end() && ++it != args.end())
        return *it;
    return std::string();
}

bool cmdOptionExists(const std::vector<std::string>& args, const std::string& option)
{
    return std::find(args.begin(), args.end(), option) != args.end();
}

int main(int argc, char** argv)
{
    STATIC_ASSERT(EXIT_STATUS_INC == 0);

    std::vector<std::string> args(argv, argv+argc);
    if (args.size() == 1
        || cmdOptionExists(args, "-h")
        || cmdOptionExists(args, "--help")) {
        std::cout << "Example usage: dfgen -i path/to/mesh.obj -o distfield.bin" << std::endl;
        return EXIT_STATUS_INC;
    }

    const std::string inputMeshPath = getCmdOption(args, "-i");
    if (inputMeshPath.length() == 0) {
        std::cout << "Input mesh file must be specified (-i)!" << std::endl;
        return EXIT_STATUS_INC;
    }

    const std::string outDistanceFieldPath = getCmdOption(args, "-o");
    if (outDistanceFieldPath.length() == 0) {
        std::cout << "Output file must be specified (-o)!" << std::endl;
        return EXIT_STATUS_INC;
    }

    std::ofstream outStream(outDistanceFieldPath, std::ios::binary);
    if (!outStream) {
        std::cout << "Failed to open output file!" << std::endl;
        return EXIT_STATUS_INC;
    }

    int k_distanceFieldSize = 64;
    const std::string sizeArg = getCmdOption(args, "--size");
    if (sizeArg.length() > 0) {
        try {
            k_distanceFieldSize = std::stoi(sizeArg);
        } catch (const std::exception&) {
            std::cout << "Failed to parse --size arg!" << std::endl;
        }
        ASSERT(k_distanceFieldSize >= 2);
    }
    std::cout << "Using distance field size: " << k_distanceFieldSize << "x"
                                               << k_distanceFieldSize << "x"
                                               << k_distanceFieldSize << std::endl;

    if (cmdOptionExists(args, "--verbose")) {
        Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE, aiDefaultLogStream_STDOUT);
    }

    Assimp::Importer assImport;
    const aiScene* assScene = assImport.ReadFile(inputMeshPath,
                                                 aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!assScene) {
        std::cout << "Assimp failed to import mesh: " << assImport.GetErrorString() << std::endl;
        return EXIT_STATUS_INC;
    }

    if (assScene->mNumMeshes != 1) {
        std::cout << "Only a single mesh currently supported!" << std::endl;
        return EXIT_STATUS_INC;
    }

    const aiMesh* mesh = assScene->mMeshes[0];

    // Build polyhedron structure out of triangles.
    CGALBuilder<Polyhedron::HalfedgeDS> builder(mesh);
    Polyhedron polyhedron;
    polyhedron.delegate(builder);
    PolyhedralMeshDomain pmd(polyhedron);
    PolyhedralMeshDomain::Is_in_domain isInDomain = pmd.is_in_domain_object();

    // Construct AABB tree.
    AABBTree tree(polyhedron.facets_begin(), polyhedron.facets_end(), polyhedron);
    tree.accelerate_distance_queries();

    // Compute the distance field on a 3D grid in the unit cube.
    // Can be stored in a e.g. 4096x64 2D texture (64x64 y slices side by side horizontally).
    // Distance quantized to 256 values (0 means on/inside the mesh).
    uint8_t* distanceField = new uint8_t[k_distanceFieldSize * k_distanceFieldSize * k_distanceFieldSize];

    std::cout << "In progress..." << std::endl;
    for (int y = 0; y < k_distanceFieldSize; ++y) {
    for (int z = 0; z < k_distanceFieldSize; ++z) {
    for (int x = 0; x < k_distanceFieldSize; ++x) {
        const float step = 1.f / static_cast<float>(k_distanceFieldSize);
        const float off = step / 2.f;
        const Point_3 query(x*step + off,
                            y*step + off,
                            z*step + off);
        const int index = y*k_distanceFieldSize*k_distanceFieldSize + z*k_distanceFieldSize + x;
        const int domain = isInDomain(query).get_value_or(0);
        if (domain == 1)
            distanceField[index] = 0; // Inside or on boundary.
        else
            distanceField[index] = static_cast<uint8_t>(std::min(std::sqrt(static_cast<float>(tree.squared_distance(query))), 1.f) * 255.f);
    }
    }
    }

    outStream.write(reinterpret_cast<char*>(distanceField),
                    k_distanceFieldSize * k_distanceFieldSize * k_distanceFieldSize * sizeof(uint8_t));
    outStream.close();
    delete [] distanceField;
    std::cout << "Computation complete." << std::endl;
    return 0;
}
