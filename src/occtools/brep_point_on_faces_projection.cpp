#include "occtools/brep_point_on_faces_projection.h"

#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <cassert>
#include "occtools/utils.h"
#include <limits>

namespace {

typedef GeomAPI_ProjectPointOnSurf Projector_t;
struct projector_compare :
    public std::binary_function<const GeomAPI_ProjectPointOnSurf*,const GeomAPI_ProjectPointOnSurf*, bool>
{
  bool operator()(const GeomAPI_ProjectPointOnSurf* p1,
                  const GeomAPI_ProjectPointOnSurf* p2) const
  {
    double p1Dist = std::numeric_limits<double>::max();
    double p2Dist = std::numeric_limits<double>::max();
    if (p1->IsDone() && p1->NbPoints() > 0)
      p1Dist = p1->LowerDistance();
    if (p2->IsDone() && p2->NbPoints() > 0)
      p2Dist = p2->LowerDistance();
    return p1Dist < p2Dist;
  }
};

} // Anonymous namespace

namespace occ {

/*! \class BRepPointOnFacesProjection
   *  \brief Framework to perform normal point projection on a soup of topologic
   *         faces
   *
   *  Internally, the utility class GeomAPI_ProjectPointOnSurf is heavily used.
   *  \n The algorithmics are pretty slow : for a point to be projected, the
   *  projection of that point is performed on each loaded TopoDS_Face with the
   *  help of GeomAPI_ProjectPointOnSurf.\n The minimal distance amongst all
   *  the projection candidates is computed to get the final projected point
   */

//! Construct an uninitialized BRepPointOnFacesProjection
BRepPointOnFacesProjection::BRepPointOnFacesProjection() :
  _solProjector(0, TopoDS_Face())
{
}

//! Construct a BRepPointOnFacesProjection and call prepare() on \p faces
BRepPointOnFacesProjection::BRepPointOnFacesProjection(const TopoDS_Shape& faces) :
  _solProjector(0, TopoDS_Face())
{
  this->prepare(faces);
}

BRepPointOnFacesProjection::~BRepPointOnFacesProjection()
{
  this->releaseMemory();
}

/*! \brief Setup the algorithm to project points on \p faces
   *  \param faces A soup of topologic faces
   */
void BRepPointOnFacesProjection::prepare(const TopoDS_Shape& faces)
{
  this->releaseMemory();
  // Allocate a projector for each face
  for (TopExp_Explorer exp(faces, TopAbs_FACE); exp.More(); exp.Next()) {
    TopoDS_Face iFace = TopoDS::Face(exp.Current());
    Handle_Geom_Surface iSurf = BRep_Tool::Surface(iFace);
    _projectors.push_back(
          std::make_pair(new Projector_t(occ::origin3d, iSurf), iFace));
  }
}

void BRepPointOnFacesProjection::releaseMemory()
{
  // Destroy allocated projectors
  BOOST_FOREACH(const ProjectorInfo_t& projector, _projectors) {
    if (projector.first != 0)
      delete projector.first;
  }
  _projectors.clear();
}

BRepPointOnFacesProjection& BRepPointOnFacesProjection::compute(const gp_Pnt& point)
{
  //QtConcurrent::map(_projectors, projection_perform(point));
  /*        QFutureWatcher<void> futureWatcher;
            futureWatcher.setFuture(
              QtConcurrent::map(_projectors,
                          boost::bind(&Projector_t::Perform,
                                 boost::bind(&ProjectorInfo_t::first, _1), point)));
            futureWatcher.waitForFinished();*/
  std::for_each(_projectors.begin(), _projectors.end(),
                boost::bind(&Projector_t::Perform,
                            boost::bind(&ProjectorInfo_t::first, _1), point));
  std::vector<ProjectorInfo_t>::const_iterator iResult =
      std::min_element(_projectors.begin(), _projectors.end(),
                       boost::bind(::projector_compare(),
                                   boost::bind(&ProjectorInfo_t::first, _1),
                                   boost::bind(&ProjectorInfo_t::first, _2)));
  assert(iResult != _projectors.end() && "always_a_minimum");
  _solProjector = *iResult;
  return *this;
}

bool BRepPointOnFacesProjection::isDone() const
{
  const Projector_t* projector = _solProjector.first;
  if (projector != 0)
    return projector->IsDone() && projector->NbPoints() > 0;
  return false;
}

const TopoDS_Face& BRepPointOnFacesProjection::solutionFace() const
{
  static TopoDS_Face emptyFace;
  if (this->isDone())
    return _solProjector.second;
  return emptyFace;
}

gp_Pnt BRepPointOnFacesProjection::solutionPoint() const
{
  if (this->isDone())
    return _solProjector.first->NearestPoint();
  return occ::origin3d;
}

std::pair<double, double> BRepPointOnFacesProjection::solutionUV() const
{
  if (this->isDone()) {
    double u, v;
    _solProjector.first->LowerDistanceParameters(u, v);
    return std::make_pair(u, v);
  }
  return std::make_pair(0., 0.);
}

gp_Vec BRepPointOnFacesProjection::solutionNormal() const
{
  if (this->isDone()) {
    double u, v;
    _solProjector.first->LowerDistanceParameters(u, v);
    return occ::normalToFaceAtUV(_solProjector.second, u, v);
  }
  return gp_Vec(0., 0., 1.);
}

} // namespace occ
