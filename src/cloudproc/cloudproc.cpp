#include "cloudproc.hpp"
#include <pcl/io/pcd_io.h>
#include "boost/format.hpp"
#include <pcl/filters/voxel_grid.h>
#include <pcl/surface/mls.h>
#include <pcl/surface/gp3.h>
#include <pcl/io/vtk_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/io/obj_io.h>
#include <pcl/surface/organized_fast_mesh.h>
#include "pcl/io/vtk_lib_io.h"
#include <pcl/point_types.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/features/normal_3d.h>
#include <pcl/surface/convex_hull.h>
#include "pcl/impl/instantiate.hpp"
#include <boost/filesystem.hpp>

#define CLOUDPROC_POINT_TYPES   \
  (pcl::PointXYZ)             \
  (pcl::PointXYZRGB)          \
  (pcl::PointNormal)          \


using namespace std;
using namespace pcl;
namespace fs = boost::filesystem;

#define FILE_OPEN_ERROR(fname) throw runtime_error( (boost::format("couldn't open %s")%fname).str() )

namespace cloudproc {

template <class CloudT>
int cloudSize(const CloudT& cloud) {
  return cloud.width * cloud.height;
}

template <class CloudT>
void setWidthToSize(const CloudT& cloud) {
  cloud->width = cloud->points.size();
  cloud->height = 1;
}

template <class T>
typename pcl::PointCloud<T>::Ptr readPCD(const std::string& pcdfile) {
  sensor_msgs::PointCloud2 cloud_blob;
  typename pcl::PointCloud<T>::Ptr cloud (new typename pcl::PointCloud<T>);
  if (pcl::io::loadPCDFile (pcdfile, cloud_blob) != 0) FILE_OPEN_ERROR(pcdfile);
  pcl::fromROSMsg (cloud_blob, *cloud);
  return cloud;
}
#define PCL_INSTANTIATE_readPCD(T) template pcl::PointCloud<T>::Ptr readPCD<T>(const std::string& pcdfile);
PCL_INSTANTIATE(readPCD, CLOUDPROC_POINT_TYPES);

template<class T>
void saveCloud(const typename pcl::PointCloud<T>& cloud, const std::string& fname) {
  std::string ext = fs::extension(fname);
  if (ext == ".pcd")   pcl::io::savePCDFileBinary(fname, cloud);
  else if (ext == ".ply") pcl::io::savePLYFile(fname, cloud, true);
  else throw std::runtime_error( (boost::format("%s has unrecognized extension")%fname).str() );
}
#define PCL_INSTANTIATE_saveCloud(T) template void saveCloud(const  pcl::PointCloud<T>& cloud, const std::string& fname);
PCL_INSTANTIATE(saveCloud, CLOUDPROC_POINT_TYPES);

///////////////////////////

template<class PointT>
typename pcl::PointCloud<PointT>::Ptr downsampleCloud(typename pcl::PointCloud<PointT>::ConstPtr in, float vsize) {
  typename pcl::PointCloud<PointT>::Ptr out (new typename pcl::PointCloud<PointT>);
  pcl::VoxelGrid< PointT > sor;
  sor.setInputCloud (in);
  sor.setLeafSize (vsize, vsize, vsize);
  sor.filter (*out);
  return out;
}
#define PCL_INSTANTIATE_downsampleCloud(PointT) template pcl::PointCloud<PointT>::Ptr downsampleCloud<PointT>(PointCloud<PointT>::ConstPtr in, float vsize);
PCL_INSTANTIATE(downsampleCloud, CLOUDPROC_POINT_TYPES);

//////////////////////////


void findConvexHull(PointCloud<pcl::PointXYZ>::ConstPtr in, pcl::PointCloud<pcl::PointXYZ>& out, std::vector<Vertices>& polygons) {
  pcl::ConvexHull<PointXYZ> chull;
  chull.setInputCloud (in);
  chull.reconstruct (out, polygons);
}

PointCloud<pcl::PointNormal>::Ptr mlsAddNormals(PointCloud<pcl::PointXYZ>::ConstPtr in, float searchRadius) {
  pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals (new pcl::PointCloud<pcl::PointNormal>);

  pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
  tree->setInputCloud (in);

  pcl::MovingLeastSquares<pcl::PointXYZ, pcl::PointNormal> mls;
  mls.setComputeNormals (true);
  mls.setInputCloud (in);
  mls.setPolynomialFit (true);
  mls.setSearchMethod (tree);
  mls.setSearchRadius (0.04);
  mls.process (*cloud_with_normals);
  return cloud_with_normals;
}
#if 0
pcl::PolygonMesh::Ptr createMesh_MarchingCubes(PointCloud<pcl::PointNormal>::ConstPtr cloud_with_normals) {
  pcl::PolygonMesh::Ptr triangles(new PolygonMesh());
    // Create search tree*
    pcl::search::KdTree<pcl::PointNormal>::Ptr tree2 (new pcl::search::KdTree<pcl::PointNormal>);
    tree2->setInputCloud (cloud_with_normals);

    pcl::MarchingCubesGreedy<pcl::PointNormal> mcg;
    mcg.setInputCloud(cloud_with_normals); 
    mcg.setSearchMethod(tree2); 
    mcg.setIsoLevel(.1);
    mcg.setGridResolution(100,100,100);
    mcg.reconstruct (*triangles);

    return triangles;
}
#endif

pcl::PolygonMesh::Ptr meshGP3(PointCloud<pcl::PointNormal>::ConstPtr cloud_with_normals, float mu, int maxnn, float searchRadius) {
  pcl::search::KdTree<pcl::PointNormal>::Ptr tree2(
      new pcl::search::KdTree<pcl::PointNormal>);
  tree2->setInputCloud(cloud_with_normals);

  // Initialize objects
  pcl::GreedyProjectionTriangulation<pcl::PointNormal> gp3;
  pcl::PolygonMesh::Ptr triangles(new pcl::PolygonMesh);

  // Set the maximum distance between connected points (maximum edge length)
  gp3.setSearchRadius(searchRadius);

  // Set typical values for the parameters
  gp3.setMu(mu);
  gp3.setMaximumNearestNeighbors(maxnn);
  gp3.setMaximumSurfaceAngle(M_PI / 4); // 45 degrees
  gp3.setMinimumAngle(M_PI / 18); // 10 degrees
  gp3.setMaximumAngle(2 * M_PI / 3); // 120 degrees
  gp3.setNormalConsistency(false);

  gp3.setInputCloud(cloud_with_normals);
  gp3.setSearchMethod(tree2);
  gp3.reconstruct(*triangles);
  return triangles;
}


pcl::PolygonMesh::Ptr meshOFM(PointCloud<pcl::PointXYZ>::ConstPtr cloud, int edgeLengthPixels, float maxEdgeLength) {
  pcl::OrganizedFastMesh<pcl::PointXYZ> ofm;
  ofm.setInputCloud(cloud);
  ofm.setTrianglePixelSize (edgeLengthPixels);
  ofm.setMaxEdgeLength(maxEdgeLength);
  ofm.setTriangulationType (pcl::OrganizedFastMesh<PointXYZ>::TRIANGLE_ADAPTIVE_CUT);
  pcl::PolygonMeshPtr mesh(new pcl::PolygonMesh());
  ofm.reconstruct(mesh->polygons);
  pcl::toROSMsg(*cloud, mesh->cloud);
  mesh->header = cloud->header;
  return mesh;
}

#if 0
pcl::PolygonMesh::Ptr createMesh(PointCloud<pcl::PointNormal>::ConstPtr cloud, MeshMethod method) {
  switch (method) {
  case MarchingCubes:
    return createMesh_MarchingCubes(cloud);
  case GP3:
    return createMesh_GP3(cloud);
  default:
    throw std::runtime_error("invalid meshing method");
  }
}
#endif

#if 0
void saveTrimeshCustomFmt(pcl::PolygonMesh::ConstPtr mesh, const std::string& fname) {
  ofstream o(fname.c_str());
  if (o.bad()) FILE_OPEN_ERROR(fname);
  pcl::PointCloud<PointXYZ>::Ptr cloud(new pcl::PointCloud<PointXYZ>());
  pcl::fromROSMsg(mesh->cloud, *cloud);
  o << cloud->size() << endl;
  BOOST_FOREACH(const PointXYZ& pt, cloud->points) {
    o << pt.x << " " << pt.y << " " << pt.z;
  }
  o << mesh->polygons.size() << endl;
  BOOST_FOREACH(const pcl::Vertices& verts, mesh->polygons) {
    if (verts.vertices.size() != 3) throw runtime_error("only triangles are supported");
    o << verts.vertices[0] << " " << verts.vertices[1] << " " << verts.vertices[2] << endl;
  }
}
#endif

template <typename T>
pcl::PointCloud<pcl::PointXYZ>::Ptr toXYZ(typename pcl::PointCloud<T>::ConstPtr in) {
  pcl::PointCloud<pcl::PointXYZ>::Ptr out(new pcl::PointCloud<PointXYZ>());
  out->reserve(in->size());
  out->width = in->width;
  out->height = in->height;
  BOOST_FOREACH(const T& pt, in->points) {
    out->points.push_back(PointXYZ(pt.x, pt.y, pt.z));
  }
  return out;
}
#define PCL_INSTANTIATE_toXYZ(T) template PointCloud<pcl::PointXYZ>::Ptr toXYZ<T>(PointCloud<T>::ConstPtr in);
PCL_INSTANTIATE(toXYZ, CLOUDPROC_POINT_TYPES);


template <class PointT>
VectorXb boxMask(typename pcl::PointCloud<PointT>::ConstPtr in, const Eigen::Vector3f& mins, const Eigen::Vector3f& maxes) {
  int i=0;
  VectorXb out(in->size());
  BOOST_FOREACH(const PointT& pt, in->points) {
    out[i] = (pt.x >= mins.x() && pt.x <= maxes.x() && pt.y >= mins.y() && pt.y <= maxes.y() && pt.z >= mins.z() && pt.z <= maxes.z());
    ++i;
  }
  return out;
}
#define PCL_INSTANTIATE_boxMask(PointT) template VectorXb boxMask<PointT>(pcl::PointCloud<PointT>::ConstPtr in, const Eigen::Vector3f& mins, const Eigen::Vector3f& maxes);
PCL_INSTANTIATE(boxMask, CLOUDPROC_POINT_TYPES);


template <class PointT>
typename pcl::PointCloud<PointT>::Ptr maskFilter(typename pcl::PointCloud<PointT>::ConstPtr in, const VectorXb& mask) {
  int n = mask.sum();
  typename pcl::PointCloud<PointT>::Ptr out(new typename pcl::PointCloud<PointT>());
  out->points.reserve(n);
  for (int i=0; i < mask.size(); ++i) {
    if (mask[i]) out->points.push_back(in->points[i]);
  }
  setWidthToSize(out);
  return out;
}
#define PCL_INSTANTIATE_maskFilter(PointT) template pcl::PointCloud<PointT>::Ptr maskFilter<PointT>(pcl::PointCloud<PointT>::ConstPtr in, const VectorXb& mask);
PCL_INSTANTIATE(maskFilter, CLOUDPROC_POINT_TYPES);



void saveMesh(const pcl::PolygonMesh& mesh, const std::string& fname) {

  string ext = fs::extension(fname);

  if (ext == ".ply") {
    pcl::io::savePLYFile (fname, mesh);
  }
  else if (ext == ".obj") {
    pcl::io::saveOBJFile (fname, mesh);
  }
  else if (ext == ".vtk") {
    pcl::io::saveVTKFile (fname, mesh);
  }
  else PRINT_AND_THROW(boost::format("filename %s had unrecognized extension")%fname);
}

}
