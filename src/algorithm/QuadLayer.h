/*
* This source file is part of the tqmesh library.  
* This code was written by Florian Setzwein in 2022, 
* and is covered under the MIT License
* Refer to the accompanying documentation for details
* on usage and license.
*/
#pragma once

#include <algorithm>

#include "VecND.h"

#include "utils.h"
#include "Vertex.h"
#include "Triangle.h"
#include "Quad.h"
#include "Front.h"
#include "Boundary.h"
#include "Domain.h"

namespace TQMesh {
namespace TQAlgorithm {

using namespace CppUtils;

/*********************************************************************
* A structure that contains the data for a quad layer generation
*
*                   proj_v1_[0]       proj_v1_[1]       proj_v1_[2]
*  proj_v1_[0]      proj_v1_[1]       proj_v1_[2]
*    ^----------------^-----------------^-----------------^-----...
*    |                |                 |                 |
*    |                |                 |                 |
*    |                |                 |                 |
*    | base_edges_[0] |  base_edges_[1] |  base_edges_[2] |
*    o----------------o-----------------o-----------------o-----...
*  base_v1_[0]      base_v1_[1]       base_v1_[2]            
*                   base_v2_[0]       base_v2_[1]       base_v2_[2]
*
*********************************************************************/
class QuadLayer
{
public:

  using DoubleVector = std::vector<double>;
  using Vec2dVector  = std::vector<Vec2d>;
  using VertexVector = std::vector<Vertex*>;
  using EdgeVector   = std::vector<Edge*>;

  /*------------------------------------------------------------------
  | Constructor
  ------------------------------------------------------------------*/
  QuadLayer(Edge& e_start, Edge& e_end, bool is_closed, double height)
  : e_start_   { &e_start }
  , e_end_     { &e_end   }
  , is_closed_ { is_closed }
  , height_    { height }
  {
    // Initialize edges
    Edge* e_cur = e_start_;

    do 
    {
      ASSERT( e_cur, "QuadLayer::QuadLayer(): "
        "Advancing front data structure seems to be corrupted.");

      add_quadlayer_edge( *e_cur );
      e_cur = e_cur->get_next_edge();

    } while ( e_cur != e_end_ );

    // Add also the ending edge
    add_quadlayer_edge( *e_end_ );

  } // QuadLayer()

  /*------------------------------------------------------------------
  | Getters
  ------------------------------------------------------------------*/
  Edge* e_start() const { return e_start_; }
  Edge* e_end() const { return e_end_; }
  bool is_closed() const { return is_closed_; }
  double height() const { return height_; }
  int n_base_edges() const 
  { 
    // Cast number of edges to int for use of modulo function  
    return static_cast<int>( base_edges_.size() ); 
  }

  const EdgeVector& base_edges() const { return base_edges_; }
  EdgeVector& base_edges() { return base_edges_; }

  const VertexVector& base_v1() const { return base_v1_; }
  const VertexVector& base_v2() const { return base_v2_; }
  const VertexVector& proj_p1() const { return proj_v1_; }
  const VertexVector& proj_p2() const { return proj_v2_; }

  VertexVector& base_v1() { return base_v1_; }
  VertexVector& base_v2() { return base_v2_; }
  VertexVector& proj_p1() { return proj_v1_; }
  VertexVector& proj_p2() { return proj_v2_; }

  const Vec2dVector& proj_p1_xy() const { return proj_v1_xy_; }
  Vec2dVector& proj_p1_xy() { return proj_v1_xy_; }

  const Vec2dVector& proj_p2_xy() const { return proj_v2_xy_; }
  Vec2dVector& proj_p2_xy() { return proj_v2_xy_; }

  const DoubleVector& heights() const { return heights_; }
  DoubleVector& heights() { return heights_; }

  /*------------------------------------------------------------------
  | Setters
  ------------------------------------------------------------------*/

  /*------------------------------------------------------------------
  | Smooth the heights of all quad projections in the layer according
  | to the local size function
  ------------------------------------------------------------------*/
  void smooth_heights(const Domain& domain)
  {
    for ( size_t i = 1; i < heights_.size()-1; ++i )
    {
      const double h1 = heights_[i-1];
      const double h2 = heights_[i];
      const double h3 = heights_[i+1];

      const Vec2d& c = base_edges_[i]->xy();
      const double rho = domain.size_function( c );

      heights_[i] = MIN(rho, (h1+h2+h3) / 3.0);
    }

  } // QuadLayer::smooth_heights()


  /*------------------------------------------------------------------
  | In this function we set up the correct coordinates of all the
  | projected base vertices. In the case of a QuadLayer that is not
  | closed, adjacent edges are eventually refined. 
  ------------------------------------------------------------------*/
  void setup_vertex_projection(Mesh& mesh, Front& front)
  {
    // Update coordinates of projected base vertices
    for ( size_t i = 1; i < base_edges_.size(); ++i )
      adjust_projected_vertex_coordinates(i-1, i);

    if ( is_closed_ )
    {
      adjust_projected_vertex_coordinates( n_base_edges()-1, 0);
    }
    else
    {
      place_start_vertex(mesh.vertices(), front, mesh.boundary_edges());
      place_end_vertex(mesh.vertices(), front, mesh.boundary_edges());
    }

  } // QuadLayer::setup_vertex_projection()



private:

  /*------------------------------------------------------------------
  | Try to adjust the initial projected base vertex coordinates,
  | such that adjacent base edges have the same coordinates of their
  | projected vertices.
  | However, in some cases this would lead to bad elements. In these
  | cases, the respective base is marked as a "wedge", where we will
  | introduce triangles later on, in order to obtain a better quality
  |
  |                   q                    r
  |                    o------------------o
  |                   /   base_edges_[j]
  |                  /
  |                 /
  |                / base_edges_[i] 
  |               / 
  |              / 
  |             o p 
  |
  ------------------------------------------------------------------*/
  void adjust_projected_vertex_coordinates(int i, int j)
  {
    const Vec2d& p = base_v1_[i]->xy();
    const Vec2d& q = base_v1_[j]->xy(); 
    const Vec2d& r = base_v2_[j]->xy();

    const double alpha = angle( p-q, r-q );

    // If both projected vertices are too far apart, we must 
    // create a wedge in between. In this case we use the default 
    // projection coordinates proj_p1_xy and proj_p2_xy that were already 
    // calculated in the constructor
    if ( is_left(p, r, q) && alpha <= CONSTANTS.quad_layer_angle() )
      return;

    // Otherwise, the projected vertex coordinate will be placed
    // in between the originally projected coordinates 
    const Vec2d& n1 = base_edges_[i]->normal();
    const double l1 = heights_[i];

    const Vec2d& n2 = base_edges_[j]->normal();
    const double l2 = heights_[j];

    const Vec2d  normal  = 0.5 * (n1 + n2);
    const double l       = 0.5 * (l1 + l2);
    const Vec2d  nn      = normal / normal.norm();

    Vec2d xy_proj = q + nn * l / sin(0.5*alpha);

    proj_v1_xy_[j] = xy_proj;
    proj_v2_xy_[i] = xy_proj;

    return;

  } // QuadLayer::adjust_projected_vertex_coordinates()


  /*------------------------------------------------------------------
  |
  ------------------------------------------------------------------*/
  void place_start_vertex(Vertices& verts, 
                          Front&    front, 
                          EdgeList& bdry_edges)
  {
    // The edge adjacent to the starting edge
    Edge* e_prv = e_start_->get_prev_edge();

    // The current starting base vertex
    Vertex& v_start = *base_v1_[0];

    ASSERT( (e_prv) && (e_prv->v2() == v_start),
    "During the generation of a QuadLayer, an invalid data\n"
    "structure of the advancing front has been provided.\n"
    "It seems that the given advancing front is not a connected\n"
    "list of edges.\n");

    // The adjacent previous vertex 
    Vertex& v_prev  = e_prv->v1();

    // If the previous vertex is located right to the starting edge,
    // we use the default projection coordinate
    if ( !is_left(base_v1_[0]->xy(), base_v2_[0]->xy(), v_prev.xy()) )
      return;

    // Check if the segment between v_start and its projected 
    // coordinate intersects with the previous edge 
    // If yes, merge them
    const double h     = heights_[0];
    const double d_fac = (v_prev.xy() - proj_v1_xy_[0]).norm() / h;

    if ( d_fac < 1.0 )
    {
      proj_v1_[0] = &v_prev;
      return;
    }

    // If the projected vertex coordinate is located within the range 
    // of the previous edge, it must be projected onto it
    //
    //                            v_prev
    //    o                     o 
    //     \                   /
    //      \                /
    //       \     proj_p1 /
    //        \   o      /   
    //         x  |    x
    //          \ |  /      
    //           \|/
    //            o---------------------o
    //            x                 
    //              v_start
    //
    //
    if ( h < e_prv->length() )
    {
      const Vec2d d1 = v_prev.xy() - v_start.xy();
      const Vec2d d2 = proj_v1_xy_[0] - v_start.xy();
      const double alpha = angle( d1, d2 );
      const double ang_fac = cos(alpha); // / sin(0.5*alpha);


      // Remove the old edge from the boundary edge list (if contained)
      Edge* e_bdry_rem = bdry_edges.get_edge(e_prv->v1(), e_prv->v2());

      if ( e_bdry_rem ) 
      {
        Edge* tmp = e_bdry_rem->get_next_edge();
        bdry_edges.remove( *e_bdry_rem );
        e_bdry_rem = tmp;
        ASSERT( e_bdry_rem, "INVALID DATA STRUCTURE" );
      }
    
      // Split the adjacent edge in two small edges
      double sf = (h * ang_fac) / e_prv->length();

      auto new_edges = front.split_edge(*e_prv, verts, sf, false);

      // Add potential new boundary edges
      if ( e_bdry_rem )
      {
        Edge* e1 = new_edges.first;
        Edge* e2 = new_edges.second;
        bdry_edges.insert_edge( e_bdry_rem->pos(),
                                e1->v1(), e1->v2(), e1->marker() );
        bdry_edges.insert_edge( e_bdry_rem->pos(),
                                e2->v1(), e2->v2(), e2->marker() );
      }

      // Add new vertex to quad layer structure
      Vertex& v_new = new_edges.first->v2();

      proj_v1_[0]    = &v_new;
      proj_v1_xy_[0] = v_new.xy();

    }
    // Projected vertex is located outside of adjacent edge range
    // --> Set p to be a
    //
    //            o proj_p1          
    //            |             
    //            |           
    //       o    |       v_prev
    //        \   x     o   
    //         \  |    /
    //          \ |  /
    //           \|/
    //            o---------------------o
    //            v_start           
    else
    {
      proj_v1_[0]    = &v_prev;
      proj_v1_xy_[0] = v_prev.xy();
    }

    return;

  } // QuadLayer::place_start_vertex()


  /*------------------------------------------------------------------
  |
  ------------------------------------------------------------------*/
  void place_end_vertex(Vertices& verts, 
                        Front&    front, 
                        EdgeList& bdry_edges)
  {
    // The edge adjacent to the starting edge
    Edge* e_nxt = e_end_->get_next_edge();

    // The current ending base vertex
    Vertex& v_end = *base_v2_.back();

    ASSERT( (e_nxt) && (e_nxt->v1() == v_end),
    "During the generation of a QuadLayer, an invalid data\n"
    "structure of the advancing front has been provided.\n"
    "It seems that the given advancing front is not a connected\n"
    "list of edges.\n");

    // The adjacent previous vertex 
    Vertex& v_next  = e_nxt->v2();

    // If the next vertex is located right to the starting edge,
    // we use the default projection coordinate
    const Vec2d& xy_end_1 = base_v1_.back()->xy();
    const Vec2d& xy_end_2 = base_v2_.back()->xy();
    //
    if ( !is_left(xy_end_1, xy_end_2, v_next.xy()) )
      return;

    // Check if the segment between v_start and its projected 
    // coordinate intersects with the previous edge 
    // If yes, merge them
    const double h     = heights_.back();
    const double d_fac = (v_next.xy() - proj_v2_xy_.back()).norm() / h;

    if ( d_fac < 1.0 )
    {
      proj_v2_.back() = &v_next;
      return;
    }

    // If the projected vertex coordinate is located within the range 
    // of the previous edge, it must be projected onto it
    //
    //            v_next
    //           o                     o 
    //            \                   /
    //             \                /
    //              \     proj_p2 /
    //               \   o      /   
    //                x  |    x
    //                 \ |  /    
    //                  \|/
    //   o---------------o
    //         base        v_end       
    //
    //
    if ( h < e_nxt->length() )
    {
      const Vec2d d1 = v_next.xy() - v_end.xy();
      const Vec2d d2 = proj_v2_xy_.back() - v_end.xy();
      const double alpha = angle( d1, d2 );
      const double ang_fac = cos(alpha); // / sin(0.5*alpha);

      // Remove the old edge from the boundary edge list (if contained)
      Edge* e_bdry_rem = bdry_edges.get_edge(e_nxt->v1(), e_nxt->v2());

      if ( e_bdry_rem ) 
      {
        Edge* tmp = e_bdry_rem->get_next_edge();
        bdry_edges.remove( *e_bdry_rem );
        e_bdry_rem = tmp;
        ASSERT( e_bdry_rem, "INVALID DATA STRUCTURE" );
      }
    
      // Split the adjacent edge in two small edges
      double sf = 1.0 - (h * ang_fac) / e_nxt->length();

      auto new_edges = front.split_edge(*e_nxt, verts, sf, false);

      // Add potential new boundary edges
      if ( e_bdry_rem )
      {
        Edge* e1 = new_edges.first;
        Edge* e2 = new_edges.second;
        bdry_edges.insert_edge( e_bdry_rem->pos(),
                                e1->v1(), e1->v2(), e1->marker() );
        bdry_edges.insert_edge( e_bdry_rem->pos(),
                                e2->v1(), e2->v2(), e2->marker() );
      }

      // Add new vertex to quad layer structure
      Vertex& v_new = new_edges.first->v2();

      proj_v2_.back() = &v_new;
      proj_v2_xy_.back() = v_new.xy();
    }
    // Projected vertex is located outside of adjacent edge range
    // --> Set p to be a
    //
    //             proj_p2
    //            o               
    //            |             
    //            |           
    //       o    |       v_next  
    //        \   x     o   
    //         \  |    /
    //          \ |  /
    //           \|/
    //  o---------o
    //             v_end
    //
    else
    {
      proj_v2_.back()    = &v_next;
      proj_v2_xy_.back() = v_next.xy();
    }

    return;

  } // QuadLayer::place_end_vertex()

  /*------------------------------------------------------------------
  | Add a new edge to the quad layer structure
  ------------------------------------------------------------------*/
  void add_quadlayer_edge(Edge& e_cur)
  {
    // Pointer to base edge
    base_edges_.push_back( &e_cur );

    // Pointers to vertices of current base edge 
    base_v1_.push_back( &e_cur.v1() );
    base_v2_.push_back( &e_cur.v2() );

    // Adjust height, in order to get good aspect ratios
    double h = MIN( height_, e_cur.length() );
    heights_.push_back( h );

    // Coordinates of initial projected base vertices 
    proj_v1_xy_.push_back( e_cur.v1().xy() + e_cur.normal() * h );
    proj_v2_xy_.push_back( e_cur.v2().xy() + e_cur.normal() * h );

    // Since the actual projected vertices are not generated yet,
    // we introduced them in terms of nullpointers
    proj_v1_.push_back( nullptr );
    proj_v2_.push_back( nullptr );

  } // QuadLayer::add_quadlayer_edge()

  /*------------------------------------------------------------------
  | Attributes
  ------------------------------------------------------------------*/
  Edge*        e_start_   {nullptr};
  Edge*        e_end_     {nullptr};
  bool         is_closed_ { false };
  double       height_    {  0.0  };

  EdgeVector   base_edges_ {};
  VertexVector base_v1_    {};
  VertexVector base_v2_    {};

  VertexVector proj_v1_    {};
  VertexVector proj_v2_    {};

  Vec2dVector  proj_v1_xy_ {};
  Vec2dVector  proj_v2_xy_ {};

  DoubleVector heights_    {};

}; // QuadLayer


} // namespace TQAlgorithm
} // namespace TQMesh
