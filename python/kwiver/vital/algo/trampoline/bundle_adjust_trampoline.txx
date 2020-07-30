/*ckwg +29
 * Copyright 2019 by Kitware, Inc.
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
 * \file bundle_adjust_trampoline.txx
 *
 * \brief trampoline for overriding virtual functions of
 *        algorithm_def<bundle_adjust> and bundle_adjust
 */

#ifndef BUNDLE_ADJUST_TRAMPOLINE_TXX
#define BUNDLE_ADJUST_TRAMPOLINE_TXX


#include <python/kwiver/vital/util/pybind11.h>
#include <pybind11/pybind11.h>
#include <python/kwiver/vital/algo/trampoline/algorithm_trampoline.txx>
#include <vital/algo/bundle_adjust.h>

namespace kwiver {
namespace vital  {
namespace python {

template <class algorithm_def_ba_base=kwiver::vital::algorithm_def<kwiver::vital::algo::bundle_adjust>>
class algorithm_def_ba_trampoline :
      public algorithm_trampoline<algorithm_def_ba_base>
{
  public:
    using algorithm_trampoline<algorithm_def_ba_base>::algorithm_trampoline;

    std::string type_name() const override
    {
      VITAL_PYBIND11_OVERLOAD(
        std::string,
        kwiver::vital::algorithm_def<kwiver::vital::algo::bundle_adjust>,
        type_name,
      );
    }
};


template <class bundle_adjust_base=kwiver::vital::algo::bundle_adjust>
class bundle_adjust_trampoline :
      public algorithm_def_ba_trampoline<bundle_adjust_base>
{
  public:
    using algorithm_def_ba_trampoline<bundle_adjust_base>::
              algorithm_def_ba_trampoline;

    void optimize( kwiver::vital::camera_map_sptr& cameras,
                   kwiver::vital::landmark_map_sptr& landmarks,
                   kwiver::vital::feature_track_set_sptr tracks,
                   kwiver::vital::sfm_constraints_sptr constraints=nullptr) const override
    {
      VITAL_PYBIND11_OVERLOAD_PURE(
        void,
        kwiver::vital::algo::bundle_adjust,
        optimize,
        cameras,
        landmarks,
        tracks,
        constraints
      );
    }

    void optimize( kwiver::vital::simple_camera_perspective_map& cameras,
                   kwiver::vital::landmark_map::map_landmark_t& landmarks,
                   kwiver::vital::feature_track_set_sptr tracks,
                   const std::set<kwiver::vital::frame_id_t>& fixed_cameras,
                   const std::set<kwiver::vital::landmark_id_t>& fixed_landmarks,
                   kwiver::vital::sfm_constraints_sptr constraints=nullptr) const override
    {
      VITAL_PYBIND11_OVERLOAD_PURE(
        void,
        kwiver::vital::algo::bundle_adjust,
        optimize,
        cameras,
        landmarks,
        tracks,
        fixed_cameras,
        fixed_landmarks,
        constraints
      );
    }
};
}}}
#endif
