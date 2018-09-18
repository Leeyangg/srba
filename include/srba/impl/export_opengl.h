/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2015, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#pragma once

#include <mrpt/opengl/CSetOfObjects.h>
#include <mrpt/opengl/stock_objects.h>
#include <mrpt/opengl/CDisk.h>
#include <mrpt/opengl/CSetOfLines.h>

#include "export_opengl_landmark_renderers.h" // Declare LandmarkRendererBase<> specializations

namespace srba {
//
// RbaEngine<>::build_opengl_representation
//
template <class KF2KF_POSE_TYPE,class LM_TYPE,class OBS_TYPE,class RBA_OPTIONS>
void RbaEngine<KF2KF_POSE_TYPE,LM_TYPE,OBS_TYPE,RBA_OPTIONS>::build_opengl_representation(
	const srba::TKeyFrameID root_keyframe,
	const TOpenGLRepresentationOptions &options,
	mrpt::opengl::CSetOfObjects::Ptr out_scene,
	mrpt::opengl::CSetOfObjects::Ptr out_root_tree
	) const
{
	using namespace std;
	using mrpt::poses::CPose3D;

	// Generate 3D scene:
	// ------------------------------
	if (out_scene)
	{
		out_scene->clear();

		if (!rba_state.keyframes.empty())
		{
			// Use a spanning tree to estimate the global pose of every node
			//  starting (root) at the given keyframe:

			frameid2pose_map_t  spantree;
			create_complete_spanning_tree(root_keyframe,spantree, options.span_tree_max_depth );

			// For each key-frame, add a 3D corner:
			for (typename frameid2pose_map_t::const_iterator itP = spantree.begin();itP!=spantree.end();++itP)
			{
				if (root_keyframe==itP->first) continue;

				const CPose3D p = CPose3D(itP->second.pose);

				const bool is_elevated_hiearchical_kf = (options.draw_kf_hierarchical && rba_state.keyframes[itP->first].adjacent_k2k_edges.size()>=2);

				mrpt::opengl::CSetOfObjects::Ptr o = mrpt::opengl::stock_objects::CornerXYZSimple(is_elevated_hiearchical_kf ? 3.0 : 0.75, is_elevated_hiearchical_kf ? 4.0 : 2.0);
				if (options.draw_kf_hierarchical) {
					// Hierarchical map:
					CPose3D p_mod = p;
					if (is_elevated_hiearchical_kf)
						p_mod.z_incr( options.draw_kf_hierarchical_height );
					o->setPose(p_mod);
				} else {
					// Flat map:
					o->setPose(p);
				}
				o->setName( mrpt::format("%d",int(itP->first)).c_str() );
				o->enableShowName();
				out_scene->insert(o);
			}
			// And a bigger one for the root:
			{
				mrpt::opengl::CSetOfObjects::Ptr o = mrpt::opengl::stock_objects::CornerXYZSimple(1.0,4.0);
				//o->setPose(...);  // At the origin
				out_scene->insert(o);
			}

			// Draw all edges between frames:
			mrpt::opengl::CSetOfLines::Ptr gl_edges = mrpt::opengl::CSetOfLines::Create();
			gl_edges->setColor(1,0,1); // Magenta, in order to not confuse them with the standard lines of a grid plane
			for (typename rba_problem_state_t::k2k_edges_deque_t::const_iterator itEdge = rba_state.k2k_edges.begin();itEdge!=rba_state.k2k_edges.end();++itEdge)
			{
				CPose3D p1;
				if (itEdge->from!=root_keyframe)
				{
					typename frameid2pose_map_t::const_iterator itN1 = spantree.find(itEdge->from);
					if(itN1==spantree.end())
						continue;
					p1 = CPose3D(itN1->second.pose);
					if (options.draw_kf_hierarchical && rba_state.keyframes[itEdge->from].adjacent_k2k_edges.size()>=2)
						p1.z_incr( options.draw_kf_hierarchical_height );
				}
				CPose3D p2;
				if (itEdge->to!=root_keyframe)
				{
					typename frameid2pose_map_t::const_iterator itN2 = spantree.find(itEdge->to);
					if(itN2==spantree.end())
						continue;
					p2 = CPose3D(itN2->second.pose);
					if (options.draw_kf_hierarchical && rba_state.keyframes[itEdge->to].adjacent_k2k_edges.size()>=2)
						p2.z_incr( options.draw_kf_hierarchical_height );
				}
				gl_edges->appendLine(p1.x(),p1.y(),p1.z(), p2.x(),p2.y(),p2.z());
			}
			out_scene->insert(gl_edges);

			// Render landmarks:
			LandmarkRendererBase<typename landmark_t::render_mode_t>::render(*this,root_keyframe,spantree, options,*out_scene);

		} // end if graph is not empty

	} // end of "out_scene"


	// Generate 2D tree:
	// ------------------------------
	if (out_root_tree)
	{
		out_root_tree->clear();

		const float NODE_RADIUS = 1;
		const float ROW_HEIGHT  = 4*NODE_RADIUS;
		const float COL_WIDTH   = 3*NODE_RADIUS;

		typename rba_problem_state_t::TSpanningTree::next_edge_maps_t::const_iterator it_st_root = rba_state.spanning_tree.sym.next_edge.find(root_keyframe);
		ASSERT_(it_st_root != rba_state.spanning_tree.sym.next_edge.end());

		const std::map<TKeyFrameID,TSpanTreeEntry> & st_root = it_st_root->second;

		std::map<TKeyFrameID,topo_dist_t>  children_depths;
		std::map<topo_dist_t,std::vector<TKeyFrameID> > children_by_depth;

		// roots are not in spanning trees ("spanning_tree.sym.next_edge")
		children_depths[root_keyframe]=0;
		children_by_depth[0].push_back(root_keyframe);

		// Go thru the tree to realize of its size:
		//size_t max_nodes_per_level = 1;
		size_t max_depth = 0;
		for (std::map<TKeyFrameID,TSpanTreeEntry>::const_iterator it=st_root.begin();it!=st_root.end();++it)
		{
			const topo_dist_t depth = it->second.distance;
			children_depths[it->first] = depth;
			mrpt::keep_max(max_depth, depth);

			std::vector<TKeyFrameID> & v = children_by_depth[depth];
			v.push_back(it->first);
			//mrpt::utils::keep_max(max_nodes_per_level, v.size());
		}

		// Generate (x,y) coords for each node:
		std::map<TKeyFrameID, mrpt::math::TPoint3Df> node_coords;

		for (size_t d=0;d<=max_depth;d++)
		{
			const float y = -(d*ROW_HEIGHT);

			std::vector<TKeyFrameID> & v = children_by_depth[d];
		 	const float row_width = v.size()*COL_WIDTH;

			for (size_t i=0;i<v.size();i++)
			{
				const float x = - 0.5f*row_width + COL_WIDTH*i;
				node_coords[v[i]] = mrpt::math::TPoint3Df(x,y,0);
			}
		}

		// Draw edges in tree:
		mrpt::opengl::CSetOfLines::Ptr gl_edges = mrpt::opengl::CSetOfLines::Create();
		gl_edges->setLineWidth(1.5);
		gl_edges->setColor_u8(mrpt::img::TColor(0xff,0xff,0x00));
		out_root_tree->insert(gl_edges);

		typename rba_problem_state_t::TSpanningTree::all_edges_maps_t::const_iterator it_edges_from_root = rba_state.spanning_tree.sym.all_edges.find(root_keyframe);
		ASSERT_(it_edges_from_root != rba_state.spanning_tree.sym.all_edges.end());

		const std::map<TKeyFrameID, typename rba_problem_state_t::k2k_edge_vector_t> edges_from_root = it_edges_from_root->second;

		for (typename std::map<TKeyFrameID, typename rba_problem_state_t::k2k_edge_vector_t>::const_iterator it=edges_from_root.begin();it!=edges_from_root.end();++it)
		{
			const typename rba_problem_state_t::k2k_edge_vector_t & edges_to_j = it->second;
			for (size_t k=0;k<edges_to_j.size();k++)
			{
				const TKeyFrameID id1 = edges_to_j[k]->from;
				const TKeyFrameID id2 = edges_to_j[k]->to;

				gl_edges->appendLine(node_coords[id1], node_coords[id2]);
			}
		}

		// And on the top, draw the nodes:
		for (std::map<TKeyFrameID, mrpt::math::TPoint3Df>::const_iterator it=node_coords.begin();it!=node_coords.end();++it)
			gl_aux_draw_node(*out_root_tree, mrpt::format("%u", static_cast<unsigned int>(it->first) ), it->second.x, it->second.y);

	} // end render "2D tree"

}


template <class KF2KF_POSE_TYPE,class LM_TYPE,class OBS_TYPE,class RBA_OPTIONS>
void RbaEngine<KF2KF_POSE_TYPE,LM_TYPE,OBS_TYPE,RBA_OPTIONS>::gl_aux_draw_node(mrpt::opengl::CSetOfObjects &soo, const std::string &label, const float x, const float y) const
{
	{
		mrpt::opengl::CDisk::Ptr obj = mrpt::opengl::CDisk::Create();
		obj->setDiskRadius(1,0);
		obj->setColor_u8(mrpt::img::TColor(0x00,0x00,0x00, 0xa0));
		obj->setLocation(x,y,0);
		soo.insert(obj);
	}

	{
		mrpt::opengl::CText3D::Ptr obj = mrpt::opengl::CText3D::Create(label);
		obj->setFont("sans");
		obj->setColor_u8(mrpt::img::TColor(0xff,0xff,0xff));
		obj->setScale(0.9);
		obj->setLocation(x-0.5,y-0.5,0);
		soo.insert(obj);
	}

}

} // End of namespaces
