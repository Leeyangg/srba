/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2015, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#pragma once

namespace srba {

template <class KF2KF_POSE_TYPE,class LM_TYPE,class OBS_TYPE,class RBA_OPTIONS>
double RbaEngine<KF2KF_POSE_TYPE,LM_TYPE,OBS_TYPE,RBA_OPTIONS>::eval_overall_squared_error() const
{
	using namespace std;

	m_profiler.enter("eval_overall_squared_error");

	double sqerr = 0;

	// ---------------------------------------------------------------------------
	// Make a list of all the observing & base KFs needed by all observations:
	// ---------------------------------------------------------------------------
	vector<bool>                         base_ids(rba_state.keyframes.size(), false);
	map<TKeyFrameID, set<TKeyFrameID> >  ob_pairs; // Minimum ID first index.

	for (typename rba_problem_state_t::all_observations_deque_t::const_iterator itO=rba_state.all_observations.begin();itO!=rba_state.all_observations.end();++itO)
	{
		const TKeyFrameID obs_id = itO->obs.kf_id;
		const TKeyFrameID base_id = itO->feat_rel_pos->id_frame_base;

		base_ids[base_id] = true;
		ob_pairs[ std::min(obs_id,base_id) ].insert( std::max(obs_id,base_id) );
	}

	// ---------------------------------------------------------------------------
	// Build all the needed shortest paths:
	// ---------------------------------------------------------------------------
	// all_rel_poses[SOURCE] |--> map[TARGET] = CPose3D of TARGET as seen from SOURCE
	TRelativePosesForEachTarget  all_rel_poses;
	for (map<TKeyFrameID, set<TKeyFrameID> >::const_iterator it1=ob_pairs.begin();it1!=ob_pairs.end();++it1)
	{
		const TKeyFrameID root = it1->first;

		// Build S.T. from this root:
		m_profiler.enter("eval_overall_squared_error.complete_ST");

		frameid2pose_map_t span_tree;
		this->create_complete_spanning_tree(root,span_tree);

		m_profiler.leave("eval_overall_squared_error.complete_ST");

		// Look for the "set_intersection":
		typename frameid2pose_map_t::const_iterator       it_have     = span_tree.begin();
		const typename frameid2pose_map_t::const_iterator it_have_end = span_tree.end();

		set<TKeyFrameID>::const_iterator         it_want     = it1->second.begin();
		const set<TKeyFrameID>::const_iterator   it_want_end = it1->second.end();

		while (it_have!=it_have_end && it_want!=it_want_end)
		{
			if (it_have->first<*it_want)
				++it_have;
			else if (*it_want<it_have->first)
				++it_want;
			else
			{
				// This means we need 1 or both of the poses:
				//   root -> *it_want
				//   *it_want -> root
				all_rel_poses[root][*it_want] =  it_have->second;
				all_rel_poses[*it_want][root] = pose_flag_t(-it_have->second.pose, it_have->second.updated);

				++it_have;
				++it_want;
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Evaluate errors:
	// ---------------------------------------------------------------------------
	for (typename rba_problem_state_t::all_observations_deque_t::const_iterator itO=rba_state.all_observations.begin();itO!=rba_state.all_observations.end();++itO)
	{
		// Actually measured pixel coords: observations[i]->obs.px

		const TKeyFrameID obs_frame_id = itO->obs.kf_id;
		const TKeyFrameID base_id = itO->feat_rel_pos->id_frame_base;
		const TRelativeLandmarkPos *feat_rel_pos = itO->feat_rel_pos;
		ASSERTDEB_(feat_rel_pos!=NULL)

		pose_t const * base_pose_wrt_observer=NULL;

		// This case can occur with feats with unknown rel.pos:
		if (base_id==obs_frame_id)
		{
			base_pose_wrt_observer = &aux_null_pose;
		}
		else
		{
			// num[SOURCE] |--> map[TARGET] = CPose3D of TARGET as seen from SOURCE
			const typename TRelativePosesForEachTarget::const_iterator itPoseMap_for_base_id = all_rel_poses.find(obs_frame_id);
			ASSERT_( itPoseMap_for_base_id != all_rel_poses.end() );

			const typename frameid2pose_map_t::const_iterator itRelPose = itPoseMap_for_base_id->second.find(base_id);
			ASSERT_( itRelPose != itPoseMap_for_base_id->second.end() );

			base_pose_wrt_observer = &itRelPose->second.pose;
		}

		// Sensor pose: base_pose_wrt_sensor = robot_pose (+) sensor_pose_on_the_robot
		typename options::internal::resulting_pose_t<typename RBA_OPTIONS::sensor_pose_on_robot_t,REL_POSE_DIMS>::pose_t base_pose_wrt_sensor(mrpt::poses::UNINITIALIZED_POSE);
		RBA_OPTIONS::sensor_pose_on_robot_t::robot2sensor( *base_pose_wrt_observer, base_pose_wrt_sensor, this->parameters.sensor_pose );

		// Stored observation:
		const array_obs_t & z_real = itO->obs.obs_arr;

		// Predict observation and compare to real obs:
		residual_t delta;
		sensor_model_t::observe_error(
			delta,
			z_real,
			base_pose_wrt_sensor,
			feat_rel_pos->pos, // Position of LM wrt its base_id
			this->parameters.sensor
			);

		sqerr+= delta.squaredNorm();
	}

	m_profiler.leave("eval_overall_squared_error");

	return sqerr;
}

} // end namespaces
