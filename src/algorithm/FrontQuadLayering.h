/*
* This source file is part of the tqmesh library.  
* This code was written by Florian Setzwein in 2022, 
* and is covered under the MIT License
* Refer to the accompanying documentation for details
* on usage and license.
*/
#pragma once

#include <vector>

#include "Vertex.h"
#include "Edge.h"
#include "Facet.h"
#include "Boundary.h"
#include "Front.h"
#include "Domain.h"
#include "Mesh.h"
#include "FrontInitializer.h"
#include "FrontAlgorithm.h"
#include "QuadLayer.h"

namespace TQMesh {
namespace TQAlgorithm {

using namespace CppUtils;

/*********************************************************************
* 
*********************************************************************/
class FrontQuadLayering : public FrontAlgorithm
{
public:
  using VertexVector   = std::vector<Vertex*>;
  using TriVector      = std::vector<Triangle*>;

  /*------------------------------------------------------------------
  | Constructor / Destructor 
  ------------------------------------------------------------------*/
  FrontQuadLayering(Mesh& mesh, const Domain& domain)
  : FrontAlgorithm(mesh, domain) {}

  ~FrontQuadLayering() {}

  /*------------------------------------------------------------------
  | Getters 
  ------------------------------------------------------------------*/
  size_t n_layers() const { return n_layers_; }
  double first_height() const { return first_height_; }
  double growth_rate() const { return growth_rate_; }
  const Vec2d& starting_position() const { return xy_start_; }
  const Vec2d& ending_position() const { return xy_end_; }

  /*------------------------------------------------------------------
  | Setters 
  ------------------------------------------------------------------*/
  void n_layers(size_t n) { n_layers_ = n; }
  void first_height(double h) { first_height_ = h; }
  void growth_rate(double r) { growth_rate_ = r; }
  void starting_position(const Vec2d& v) { xy_start_ = v; }
  void starting_position(double x, double y) { xy_start_ = {x,y}; }
  void ending_position(const Vec2d& v) { xy_end_ = v; }
  void ending_position(double x, double y) { xy_end_ = {x,y}; }

  /*------------------------------------------------------------------
  | 
  ------------------------------------------------------------------*/
  bool generate_elements() override
  {
    if (mesh_.n_boundary_edges() < 1)
      return false;

    // Prepare the mesh  
    Cleanup::setup_facet_connectivity(mesh_);
    
    // Initialize the advancing front and its base edge
    init_advancing_front(false);

    // Remove invalid mesh edges that are no longer needed
    remove_invalid_mesh_edges();

    // Perform the actual mesh generation
    double height = first_height_;
    bool success = true;
    for ( size_t i_layer = 0; i_layer < n_layers_; ++i_layer)
    {
      success = generate_quad_layer(height);

      if (!success) break;

      height *= growth_rate_;
    }

    // Finish mesh structure for output
    finish_mesh_for_output();

    // Remove remaining edges from the front
    front_.clear_edges();

    return success;

  } // generate_elements()

private:

  /*------------------------------------------------------------------
  | 
  ------------------------------------------------------------------*/
  bool generate_quad_layer(double height)
  {
    // Find closest vertices in current front structure to  
    // current start and ending vertex coordinates
    Vertex* v_start        = nullptr;
    Vertex* v_end          = nullptr;
    double d_start_sqr_min = 1.0E+10;
    double d_end_sqr_min   = 1.0E+10;

    for ( const auto& e_ptr : front_ )
    {
      Vertex& v1 = e_ptr->v1();
      const double d_start_sqr = (xy_start_ - v1.xy()).norm_sqr();
      const double d_end_sqr = (xy_end_ - v1.xy()).norm_sqr();

      if ( d_start_sqr < d_start_sqr_min )
      {
        v_start = &v1;
        d_start_sqr_min = d_start_sqr;
      }

      if ( d_end_sqr < d_end_sqr_min )
      {
        v_end = &v1;
        d_end_sqr_min = d_end_sqr;
      }
    }

    if ( !v_start || !v_end )
      return false;

    // Get advancing front edges adjacent to input vertices
    Edge* e_start = front_.get_edge(*v_start, 1); 
    Edge* e_end   = front_.get_edge(*v_end, 2); 

    if ( !e_start || !e_end )
      return false;

    if ( !front_.is_traversable(*e_start, *e_end) )
      return false;

    bool is_closed = (v_start == v_end);

    // For closed quad layers, try not to start at sharp angle edges
    if ( is_closed )
    {
      const Vec2d& v1 = e_end->v1().xy();
      const Vec2d& v2 = e_end->v2().xy();
      const Vec2d& v3 = e_start->v2().xy();

      const double ang = angle(v1-v2, v3-v2);

      Edge *e_next = e_start->get_next_edge();

      if ( e_next && ang <= quad_layer_angle_ )
      {
        e_end = e_start;
        e_start = e_next; 
      }
    }

    // Create the quad layer structure, which keeps track of the target
    // vertex coordinates, that are projected from the base vertex 
    // coordinates
    QuadLayer quad_layer { e_start, e_end, is_closed, height };
    quad_layer.smooth_heights( domain_ );
    quad_layer.setup_vertex_projection( mesh_, front_ );

    // For each base edge in the quad layer, try to create a quad
    // element with its given projected coordinates
    create_quad_layer_elements( quad_layer );

    // Triangulate the quad layer based edges, where the generation
    // of quads did not succeed
    finish_quad_layer( quad_layer );

    // Remove deleted entities
    mesh_.clear_waste();

    // Set new start and ending vertex coordinates
    int i = 0;
    int n = quad_layer.n_bases();


    Vertex* v_start_in = nullptr;
    Vertex* v_end_in = nullptr;

    do 
    {
      v_start_in = quad_layer.p1()[i];

      if ( is_closed )
        v_end_in = v_start_in;
      else
        v_end_in = quad_layer.p2()[MOD(i-1,n)]; 

      ++i;

    } while ( !(v_start_in->on_front()) && !(v_end_in->on_front()) );

    if ( !v_start_in || !v_end_in )
      return false;

    xy_start_ = v_start_in->xy();
    xy_end_ = v_end_in->xy();

    return true;

  } // generate_quad_layer()


  /*------------------------------------------------------------------
  | For each QuadProjection, create a triangle with its base 
  | vertices (b1,b2) and a vertex p1, which is either located in 
  | the vicinity of the base edge or which is otherwise generated at 
  | the projected coordinate of the base vertex b1
  |   
  |           p1            p2
  |          x-------------x-------------
  |          | \           | \          |
  |          |   \         |   \        |
  |          |     \       |     \      |
  |          |       \     |       \    |
  |          |         \   |         \  |
  |          |    base   \ |           \|
  | ---------x-------------x------------x-------
  |           b1            b2
  |   
  ------------------------------------------------------------------*/
  void create_quad_layer_elements(QuadLayer& quad_layer)
  {
    auto& b1        = quad_layer.b1();
    auto& b2        = quad_layer.b2();

    auto& p1        = quad_layer.p1();
    auto& p2        = quad_layer.p2();

    auto& p1_xy     = quad_layer.p1_xy();
    auto& p2_xy     = quad_layer.p2_xy();

    auto& heights   = quad_layer.heights();
    auto& bases     = quad_layer.bases();

    int  n_bases   = quad_layer.n_bases();

    for ( int i = 0; i < n_bases; ++i )
    {
      DEBUG_LOG("QUAD LAYER BASE " << i);

      // Search radius for vertices in the vicinity of the 
      // projected coordinates
      const double r = quad_layer_range_ * heights[i];

      // Create first triangle (b1,b2,p1)
      Edge* base = bases[i];

      if (!base->in_container())
        continue;

      Triangle* t1 
        = front_update_.update_front(*base, p1_xy[i], p1_xy[i], r);

      if ( t1 == nullptr ) 
        continue;

      p1[i] = &(t1->v3());

      // Create second triangle (p1,b2,p2)
      base = front_.get_edge( *p1[i], *b2[i] );

      if ( !base ) 
        continue;

      Triangle* t2 
        = front_update_.update_front(*base, p2_xy[i], p2_xy[i], r);

      if ( t2 == nullptr )
        continue;

      p2[i] = &(t2->v3());

      // Merge both triangles t1 & t2 to a quad
      // --> First remove the interior edge between these triangles
      Edge* e_rem = mesh_.interior_edges().get_edge( *b2[i], *p1[i] );

      if ( e_rem == nullptr ) 
        continue;

      mesh_.remove_interior_edge( *e_rem );

      // Remove old triangular elements
      mesh_.remove_triangle( *t1 );
      mesh_.remove_triangle( *t2 );

      // Create new quadrilateral element
      Quad& q_new = mesh_.add_quad( *b1[i], *b2[i], *p2[i], *p1[i] );
      q_new.is_active( true );
    }

  } // Mesh::create_quad_layer_elements() 

  /*------------------------------------------------------------------
  | In some cases, gaps might be formed during the previous quad layer
  | generation steps. In this function, these gaps are closed with 
  | triangular elements.
  |
  |              p1[i]
  |      v      x 
  |     x       :
  |             :
  |  p2[i-1]    :  
  |   x.........x-------------x
  |             | b1[i]        b2[i]
  |             |           
  |             |
  |             |
  |             x
  |               
  ------------------------------------------------------------------*/
  void finish_quad_layer(QuadLayer& quad_layer)
  {
    auto& b1        = quad_layer.b1();

    auto& p1        = quad_layer.p1();
    auto& p2        = quad_layer.p2();

    int  n_bases   = quad_layer.n_bases();

    for ( int i = 1; i < n_bases; ++i )
    {
      if ( !p1[i] || !p2[i-1] || p1[i] == p2[i-1] )
        continue;

      Vertex& a = *p2[i-1];
      Vertex& b = *b1[i];
      Vertex& c = *p1[i];

      const Vec2d l1 = a.xy()-b.xy();
      const Vec2d l2 = c.xy()-b.xy();
      const double alpha = angle(l1,l2);

      // Don't add a new vertex and instead only connect (a,b,c)
      if ( alpha <= quad_layer_angle_ )
      {
        Triangle* t_new = &( mesh_.add_triangle(a, b, c) );

        if ( !front_update_.remove_from_mesh_if_invalid(*t_new) )
        {
          Edge* base = front_.get_edge( b, c );
          front_update_.advance_front( *base, a, *t_new );
        }
      }
      // Create new vertex and then generate two triangles
      else
      {
        const Vec2d v_xy = b.xy() + l1 + l2;

        Vertex& v_new = mesh_.add_vertex( v_xy );

        Triangle& t1_new = mesh_.add_triangle(a, b, v_new);
        Triangle& t2_new = mesh_.add_triangle(b, c, v_new);

        if ( !front_update_.remove_from_mesh_if_invalid(v_new, t1_new, t2_new) )
        {
          Edge* base = nullptr;

          base = front_.get_edge( a, b );
          front_update_.advance_front( *base, v_new, t1_new );

          base = front_.get_edge( b, c );
          front_update_.advance_front( *base, v_new, t2_new );

          v_new.is_fixed( true );
        }
      }
    }

  } // Mesh::finish_quad_layer()



  /*------------------------------------------------------------------
  | Attributes
  ------------------------------------------------------------------*/
  size_t n_layers_     {};
  double first_height_ {};
  double growth_rate_  {};
  Vec2d  xy_start_     {};
  Vec2d  xy_end_       {};

  double quad_layer_angle_ = 1.57079633; // = 1/2 pi
  double quad_layer_range_ = 0.75;


}; // FrontQuadLayering

} // namespace TQAlgorithm
} // namespace TQMesh
