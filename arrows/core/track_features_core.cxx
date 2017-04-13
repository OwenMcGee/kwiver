/*ckwg +29
 * Copyright 2013-2016 by Kitware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither name of Kitware, Inc. nor the names of any contributors may be used
 *    to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * \brief Implementation of track_features_core
 */

#include "track_features_core.h"
#include "merge_tracks.h"

#include <algorithm>
#include <numeric>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>

#include <vital/vital_foreach.h>
#include <vital/algo/detect_features.h>
#include <vital/algo/extract_descriptors.h>
#include <vital/algo/match_features.h>
#include <vital/algo/close_loops.h>

#include <vital/exceptions/algorithm.h>
#include <vital/exceptions/image.h>

using namespace kwiver::vital;

namespace kwiver {
namespace arrows {
namespace core {


/// Private implementation class
class track_features_core::priv
{
public:
  /// Constructor
  priv()
  {
  }

  /// The feature detector algorithm to use
  vital::algo::detect_features_sptr detector;

  /// The descriptor extractor algorithm to use
  vital::algo::extract_descriptors_sptr extractor;

  /// The feature matching algorithm to use
  vital::algo::match_features_sptr matcher;

  /// The loop closure algorithm to use
  vital::algo::close_loops_sptr closer;
};


/// Default Constructor
track_features_core
::track_features_core()
: d_(new priv)
{
}


/// Destructor
track_features_core
::~track_features_core() VITAL_NOTHROW
{
}


/// Get this alg's \link vital::config_block configuration block \endlink
vital::config_block_sptr
track_features_core
::get_configuration() const
{
  // get base config from base class
  vital::config_block_sptr config = algorithm::get_configuration();

  // Sub-algorithm implementation name + sub_config block
  // - Feature Detector algorithm
  algo::detect_features::
    get_nested_algo_configuration("feature_detector", config, d_->detector);

  // - Descriptor Extractor algorithm
  algo::extract_descriptors::
    get_nested_algo_configuration("descriptor_extractor", config, d_->extractor);

  // - Feature Matcher algorithm
  algo::match_features::
    get_nested_algo_configuration("feature_matcher", config, d_->matcher);

  // - Loop closure algorithm
  algo::close_loops::
    get_nested_algo_configuration("loop_closer", config, d_->closer);

  return config;
}


/// Set this algo's properties via a config block
void
track_features_core
::set_configuration(vital::config_block_sptr in_config)
{
  // Starting with our generated config_block to ensure that assumed values are present
  // An alternative is to check for key presence before performing a get_value() call.
  vital::config_block_sptr config = this->get_configuration();
  config->merge_config(in_config);

  // Setting nested algorithm instances via setter methods instead of directly
  // assigning to instance property.
  algo::detect_features_sptr df;
  algo::detect_features::set_nested_algo_configuration("feature_detector", config, df);
  d_->detector = df;

  algo::extract_descriptors_sptr ed;
  algo::extract_descriptors::set_nested_algo_configuration("descriptor_extractor", config, ed);
  d_->extractor = ed;

  algo::match_features_sptr mf;
  algo::match_features::set_nested_algo_configuration("feature_matcher", config, mf);
  d_->matcher = mf;

  algo::close_loops_sptr cl;
  algo::close_loops::set_nested_algo_configuration("loop_closer", config, cl);
  d_->closer = cl;
}


bool
track_features_core
::check_configuration(vital::config_block_sptr config) const
{
  bool config_valid = true;
  // this algorithm is optional
  if (config->has_value("loop_closer") &&
      config->get_value<std::string>("loop_closer") != "" &&
      !algo::close_loops::check_nested_algo_configuration("loop_closer", config))
  {
    config_valid = false;
  }
  return (
    algo::detect_features::check_nested_algo_configuration("feature_detector", config)
    &&
    algo::extract_descriptors::check_nested_algo_configuration("descriptor_extractor", config)
    &&
    algo::match_features::check_nested_algo_configuration("feature_matcher", config)
    &&
    config_valid
  );
}


/// Extend a previous set of tracks using the current frame
track_set_sptr
track_features_core
::track(track_set_sptr prev_tracks,
        unsigned int frame_number,
        image_container_sptr image_data,
        image_container_sptr mask) const
{
  // verify that all dependent algorithms have been initialized
  if( !d_->detector || !d_->extractor || !d_->matcher )
  {
    // Something did not initialize
    throw vital::algorithm_configuration_exception(this->type_name(), this->impl_name(),
        "not all sub-algorithms have been initialized");
  }

  // Check that the given mask, when non-zero, matches the size of the image
  // data provided
  if( mask && mask->size() > 0 &&
      ( image_data->width() != mask->width() ||
        image_data->height() != mask->height() ) )
  {
    throw image_size_mismatch_exception(
        "Core track feature algorithm given a non-zero mask image that is "
        "not the same shape as the provided image data.",
        image_data->width(), image_data->height(),
        mask->width(), mask->height()
        );
  }

  track_set_sptr existing_set;
  feature_set_sptr curr_feat;
  descriptor_set_sptr curr_desc;

  // see if there are already existing tracks on this frame
  if( prev_tracks )
  {
    existing_set = prev_tracks->active_tracks(frame_number);
    if( existing_set && existing_set->size() > 0 )
    {
      LOG_DEBUG( logger(), "Using existing features on frame "<<frame_number);
      // use existing features
      curr_feat = existing_set->frame_features(frame_number);

      // use existng descriptors
      curr_desc = existing_set->frame_descriptors(frame_number);
    }
  }
  if( !curr_feat || curr_feat->size() == 0 )
  {
    LOG_DEBUG( logger(), "Computing new features on frame "<<frame_number);
    // detect features on the current frame
    curr_feat = d_->detector->detect(image_data, mask);
  }
  if( !curr_desc || curr_desc->size() == 0 )
  {
    LOG_DEBUG( logger(), "Computing new descriptors on frame "<<frame_number);
    // extract descriptors on the current frame
    curr_desc = d_->extractor->extract(image_data, curr_feat, mask);
  }

  std::vector<feature_sptr> vf = curr_feat->features();
  std::vector<descriptor_sptr> df = curr_desc->descriptors();

  track_id_t next_track_id = 0;

  // special case for the first frame
  if( !prev_tracks )
  {
    typedef std::vector<feature_sptr>::const_iterator feat_itr;
    typedef std::vector<descriptor_sptr>::const_iterator desc_itr;
    feat_itr fit = vf.begin();
    desc_itr dit = df.begin();
    std::vector<vital::track_sptr> new_tracks;
    for(; fit != vf.end() && dit != df.end(); ++fit, ++dit)
    {
       track::track_state ts(frame_number, *fit, *dit);
       new_tracks.push_back(vital::track_sptr(new vital::track(ts)));
       new_tracks.back()->set_id(next_track_id++);
    }
    if( d_->closer )
    {
      // call loop closure on the first frame to establish this
      // frame as the first frame for loop closing purposes
      return d_->closer->stitch(frame_number,
                                track_set_sptr(new simple_track_set(new_tracks)),
                                image_data, mask);
    }
    return track_set_sptr(new simple_track_set(new_tracks));
  }

  // get the last track id in the existing set of tracks and increment it
  next_track_id = (*prev_tracks->all_track_ids().crbegin()) + 1;

  const vital::frame_id_t last_frame = prev_tracks->last_frame();
  vital::frame_id_t prev_frame = last_frame;

  track_set_sptr active_set;
  // if processing out of order, see if there are tracks on the previous frame
  // and prefer those over the last frame (i.e. largest frame number)
  if( prev_frame >= frame_number && frame_number > 0 )
  {
    active_set = prev_tracks->active_tracks(frame_number - 1);
    if( active_set && active_set->size() > 0 )
    {
      prev_frame = frame_number - 1;
    }
  }
  if( !active_set )
  {
    active_set = prev_tracks->active_tracks(prev_frame);
  }

  // detect features on the previous frame
  feature_set_sptr prev_feat = active_set->frame_features(prev_frame);
  // extract descriptors on the previous frame
  descriptor_set_sptr prev_desc = active_set->frame_descriptors(prev_frame);

  // match features to from the previous to the current frame
  match_set_sptr mset = d_->matcher->match(prev_feat, prev_desc,
                                           curr_feat, curr_desc);
  if( !mset )
  {
    LOG_WARN( logger(), "Feature matching between frames " << prev_frame <<
                        " and "<<frame_number<<" failed" );
    return prev_tracks;
  }

  std::vector<track_sptr> active_tracks = active_set->tracks();
  std::vector<match> vm = mset->matches();

  track_set_sptr updated_track_set;
  // if we previously had tracks on this frame, stitch to a previous frame
  if( existing_set && existing_set->size() > 0 )
  {
    std::vector<track_sptr> existing_tracks = existing_set->tracks();
    track_pairs_t track_matches;
    VITAL_FOREACH(match m, vm)
    {
      track_sptr tp = active_tracks[m.first];
      track_sptr tc = existing_tracks[m.second];
      track_matches.push_back( std::make_pair(tc, tp) );
    }
    track_map_t track_replacement;
    int num_linked = merge_tracks(track_matches, track_replacement);
    LOG_DEBUG( logger(), "Stitched " << num_linked <<
                         " existing tracks from frame " << frame_number <<
                         " to " << prev_frame );
    updated_track_set = remove_replaced_tracks(prev_tracks, track_replacement);
  }
  else
  {
    std::set<unsigned> matched;

    VITAL_FOREACH(match m, vm)
    {
      track_sptr t = active_tracks[m.first];
      track::track_state ts(frame_number, vf[m.second], df[m.second]);
      if( t->append(ts) || t->insert(ts) )
      {
        matched.insert(m.second);
      }
    }

    // find the set of unmatched active track indices
    std::vector< unsigned int > unmatched;
    std::back_insert_iterator< std::vector< unsigned int > > unmatched_insert_itr(unmatched);

    //
    // Generate a sequence of numbers
    //
    std::vector< unsigned int > sequence( vf.size() );
    std::iota(sequence.begin(), sequence.end(), 0);

    std::set_difference( sequence.begin(), sequence.end(),
                         matched.begin(), matched.end(),
                         unmatched_insert_itr );

    std::vector<track_sptr> all_tracks = prev_tracks->tracks();
    VITAL_FOREACH(unsigned i, unmatched)
    {
      track::track_state ts(frame_number, vf[i], df[i]);
      all_tracks.push_back(std::make_shared<vital::track>(ts));
      all_tracks.back()->set_id(next_track_id++);
    }
    updated_track_set = std::make_shared<simple_track_set>(all_tracks);
  }

  // run loop closure if enabled
  if( d_->closer )
  {
    updated_track_set = d_->closer->stitch(frame_number,
                                           updated_track_set,
                                           image_data, mask);
  }

  return updated_track_set;
}

} // end namespace core
} // end namespace arrows
} // end namespace kwiver
