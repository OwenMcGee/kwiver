// This file is part of KWIVER, and is distributed under the
// OSI-approved BSD 3-Clause License. See top-level LICENSE file or
// https://github.com/Kitware/kwiver/blob/master/LICENSE for details.

#include "pixel_feature_extractor.h"

#include "aligned_edge_detection.h"
#include "average_frames.h"
#include "color_commonality_filter.h"
#include "convert_image.h"
#include "high_pass_filter.h"

#include <arrows/vxl/image_container.h>
#include <vital/config/config_block_io.h>

#include <vil/vil_convert.h>
#include <vil/vil_image_view.h>
#include <vil/vil_math.h>
#include <vil/vil_plane.h>

#include <cstdlib>
#include <limits>
#include <type_traits>

namespace kwiver {

namespace arrows {

namespace vxl {

// ----------------------------------------------------------------------------
// Private implementation class
class pixel_feature_extractor::priv
{
public:

  priv( pixel_feature_extractor* parent ) : p{ parent }
  {
  }

  // Check the configuration of the sub algoirthms
  bool check_sub_algorithm( vital::config_block_sptr config, std::string key );
  // Generate the spatial encoding image
  vil_image_view< vxl_byte >
  generate_spatial_prior( kwiver::vital::image_container_sptr input_image );
  // Copy multiple filtered images into contigious memory
  template < typename pix_t > vil_image_view< pix_t >
  concatenate_images( std::vector< vil_image_view< pix_t > > filtered_images );
  // Extract local pixel-wise features
  template < typename response_t > vil_image_view< response_t >
  filter( kwiver::vital::image_container_sptr input_image );

  pixel_feature_extractor* p;

  unsigned frame_number{ 0 };
  vil_image_view< vxl_byte > spatial_prior;

  bool enable_color{ true };
  bool enable_gray{ true };
  bool enable_aligned_edge{ true };
  bool enable_average{ true };
  bool enable_color_commonality{ true };
  bool enable_high_pass_bidir{ true };
  bool enable_high_pass_box{ true };
  bool enable_normalized_variance{ true };
  bool enable_spatial_prior{ true };

  float variance_scale_factor{ 0.32 };
  unsigned grid_length{ 5 };

  std::shared_ptr< vxl::aligned_edge_detection >
  aligned_edge_detection_filter =
    std::make_shared< vxl::aligned_edge_detection >();
  std::shared_ptr< vxl::average_frames > average_frames_filter =
    std::make_shared< vxl::average_frames >();
  std::shared_ptr< vxl::convert_image > convert_filter =
    std::make_shared< vxl::convert_image >();
  std::shared_ptr< vxl::color_commonality_filter > color_commonality_filter =
    std::make_shared< vxl::color_commonality_filter >();
  std::shared_ptr< vxl::high_pass_filter > high_pass_bidir_filter =
    std::make_shared< vxl::high_pass_filter >();
  std::shared_ptr< vxl::high_pass_filter > high_pass_box_filter =
    std::make_shared< vxl::high_pass_filter >();

  std::map< std::string,
            std::shared_ptr< vital::algo::image_filter > > filters{
    std::make_pair( "aligned_edge", aligned_edge_detection_filter ),
    std::make_pair( "average", average_frames_filter ),
    std::make_pair( "convert", convert_filter ),
    std::make_pair( "color_commonality", color_commonality_filter ),
    std::make_pair( "high_pass_bidir", high_pass_bidir_filter ),
    std::make_pair( "high_pass_box", high_pass_box_filter ) };
};

// ----------------------------------------------------------------------------
bool
pixel_feature_extractor::priv
::check_sub_algorithm( vital::config_block_sptr config, std::string key )
{
  auto enabled = config->get_value< bool >( "enable_" + key );

  if( !enabled )
  {
    return true;
  }
  auto subblock = config->subblock_view( key );
  if( !filters.at( key )->check_configuration( subblock ) )
  {
    LOG_ERROR(
      p->logger(),
      "Sub-algorithm " << key << " failed its config check" );
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
vil_image_view< vxl_byte >
pixel_feature_extractor::priv
::generate_spatial_prior( kwiver::vital::image_container_sptr input_image )
{
  auto const image_data =
    vxl::image_container::vital_to_vxl( input_image->get_image() );
  // Return the previously-computed one if the size is the same
  if( spatial_prior.ni() == image_data->ni() &&
      spatial_prior.nj() == image_data->nj() )
  {
    return spatial_prior;
  }

  spatial_prior = vil_image_view< vxl_byte >( image_data->ni(),
                                              image_data->nj(), 1 );

  double const scale_factor =
    static_cast< double >( std::numeric_limits< vxl_byte >::max() ) /
    ( grid_length * grid_length - 1 );

  for( unsigned i = 0; i < image_data->ni(); i++ )
  {
    auto const i_id =
      static_cast< vxl_byte >( ( grid_length * i ) / image_data->ni() );
    for( unsigned j = 0; j < image_data->nj(); j++ )
    {
      auto const j_id =
        static_cast< vxl_byte >( ( grid_length * j ) / image_data->nj() );
      auto const index = grid_length * j_id + i_id;
      spatial_prior( i, j ) = static_cast< vxl_byte >( index * scale_factor );
    }
  }
  return spatial_prior;
}

// ----------------------------------------------------------------------------
template < typename pix_t >
vil_image_view< pix_t >
pixel_feature_extractor::priv
::concatenate_images( std::vector< vil_image_view< pix_t > > filtered_images )
{
  // Count the total number of planes
  unsigned total_planes{ 0 };

  for( auto const& image : filtered_images )
  {
    total_planes += image.nplanes();
  }

  if( total_planes == 0 )
  {
    LOG_ERROR( p->logger(), "No filtered images provided" );
    return {};
  }

  auto const ni = filtered_images.at( 0 ).ni();
  auto const nj = filtered_images.at( 0 ).nj();
  vil_image_view< pix_t > concatenated_planes{ ni, nj, total_planes };

  // Concatenate the filtered images into a single output
  unsigned current_plane{ 0 };

  for( auto const& filtered_image : filtered_images )
  {
    for( unsigned i{ 0 }; i < filtered_image.nplanes(); ++i )
    {
      auto output_plane = vil_plane( concatenated_planes, current_plane );
      auto input_plane = vil_plane( filtered_image, i );
      output_plane.deep_copy( input_plane );
      ++current_plane;
    }
  }
  return concatenated_planes;
}

// ----------------------------------------------------------------------------
// Convert to a narrower type without wrapping
template < typename out_t, typename in_t >
vil_image_view< out_t >
safe_narrowing_cast( vil_image_view< in_t > input_image )
{
  auto const ni = input_image.ni();
  auto const nj = input_image.nj();
  auto const np = input_image.nplanes();
  vil_image_view< out_t > output_image{ ni, nj, np };

  constexpr out_t max_output_value = std::numeric_limits< out_t >::max();
  constexpr out_t min_output_value = std::numeric_limits< out_t >::min();

  vil_transform(
    input_image, output_image,
    [=]( in_t pixel ){
      if( pixel < min_output_value )
      {
        return min_output_value;
      }
      else if( pixel > max_output_value )
      {
        return max_output_value;
      }
      else
      {
        return static_cast< out_t >( pixel );
      }
    } );
  return output_image;
}

// ----------------------------------------------------------------------------
template < typename pix_t >
vil_image_view< pix_t >
convert_to_typed_vil_image_view(
  kwiver::vital::image_container_sptr input_image,
  bool input_has_larger_range = false )
{
  auto const vxl_image_ptr = vxl::image_container::vital_to_vxl(
    input_image->get_image() );

  if( !input_has_larger_range )
  {
    auto const concrete_image = vil_convert_cast( pix_t(), vxl_image_ptr );
    return concrete_image;
  }

  auto const double_image = static_cast< vil_image_view< double > >(
    vil_convert_cast( double(), vxl_image_ptr ) );
  auto const concrete_image = safe_narrowing_cast< pix_t >( double_image );
  return concrete_image;
}

// ----------------------------------------------------------------------------
template < typename pix_t >
vil_image_view< pix_t >
pixel_feature_extractor::priv
::filter( kwiver::vital::image_container_sptr input_image )
{
  ++frame_number;

  std::vector< vil_image_view< pix_t > > filtered_images;

  if( enable_color || enable_gray )
  {
    const auto vxl_image =
      convert_to_typed_vil_image_view< pix_t >( input_image );

    // 3 channels
    if( enable_color )
    {
      filtered_images.push_back( vxl_image );
    }

    // 1 channel
    if( enable_gray )
    {
      // TODO consider vil_convert_to_grey_using_rgb_weighting
      auto vxl_gray_sptr =
        vil_convert_to_grey_using_average(
          vxl::image_container::vital_to_vxl( input_image->get_image() ) );
      auto vxl_gray = vil_convert_cast( pix_t(), vxl_gray_sptr );
      filtered_images.push_back( vxl_gray );
    }
  }

  if( enable_color_commonality )
  {
    // 1 channel
    auto color_commonality = convert_to_typed_vil_image_view< pix_t >(
      color_commonality_filter->filter( input_image ) );

    filtered_images.push_back( color_commonality );
  }
  if( enable_high_pass_box )
  {
    auto high_pass_box = convert_to_typed_vil_image_view< pix_t >(
      high_pass_box_filter->filter( input_image ) );

    // Legacy BurnOut models expect these channels to be incorrectly ordered
    // Swap the ordering to accomodate models trained in legacy BurnOut
    auto first_plane = vil_plane( high_pass_box, 0 );
    auto second_plane = vil_plane( high_pass_box, 1 );
    auto temp = vil_copy_deep( first_plane );
    temp.deep_copy( first_plane );
    first_plane.deep_copy( second_plane );
    second_plane.deep_copy( temp );

    // 3 channels
    filtered_images.push_back( high_pass_box );
  }
  if( enable_high_pass_bidir )
  {
    auto high_pass_bidir = convert_to_typed_vil_image_view< pix_t >(
      high_pass_bidir_filter->filter( input_image ) );
    // 3 channels
    filtered_images.push_back( high_pass_bidir );
  }

  kwiver::vital::image_container_sptr variance_container;

  if( enable_average || enable_normalized_variance )
  {
    // This is only used internally and isn't externally configurable
    vital::config_block_sptr convert_config =
      vital::config_block::empty_config();
    convert_config->set_value( "single_channel", true );
    convert_filter->set_configuration( convert_config );

    auto grayscale = convert_filter->filter( input_image );
    variance_container = average_frames_filter->filter( grayscale );
  }

  // TODO consider naming this variance since that option is used more
  if( enable_average )
  {
    auto variance = convert_to_typed_vil_image_view< pix_t >(
      variance_container, true );
    // 1 channel
    filtered_images.push_back( variance );
  }
  if( enable_aligned_edge )
  {
    auto aligned_edge = convert_to_typed_vil_image_view< pix_t >(
      aligned_edge_detection_filter->filter( input_image ) );

    auto joint_response =
      vil_plane( aligned_edge, aligned_edge.nplanes() - 1 );
    // 1 channel
    filtered_images.push_back( joint_response );
  }
  if( enable_normalized_variance )
  {
    // Since variance is a double and may be small, avoid premptively casting
    // to a byte
    auto double_variance =
      convert_to_typed_vil_image_view< double >( variance_container );
    auto scale_factor =
      variance_scale_factor / static_cast< float >( frame_number );
    vil_math_scale_values( double_variance, scale_factor );

    auto variance = safe_narrowing_cast< pix_t >( double_variance );
    // 1 channel
    filtered_images.push_back( variance );
  }
  if( enable_spatial_prior )
  {
    auto spatial_prior = generate_spatial_prior( input_image );
    // 1 channel
    filtered_images.push_back( spatial_prior );
  }

  vil_image_view< pix_t > concatenated_out =
    concatenate_images< pix_t >( filtered_images );

  return concatenated_out;
}

// ----------------------------------------------------------------------------
pixel_feature_extractor
::pixel_feature_extractor()
  : d{ new priv{ this } }
{
  attach_logger( "arrows.vxl.pixel_feature_extractor" );
}

// ----------------------------------------------------------------------------
pixel_feature_extractor
::~pixel_feature_extractor()
{
}

// ----------------------------------------------------------------------------
vital::config_block_sptr
pixel_feature_extractor
::get_configuration() const
{
  // get base config from base class
  vital::config_block_sptr config = algorithm::get_configuration();

  config->set_value( "enable_color",
                     d->enable_color,
                     "Enable color channels." );
  config->set_value( "enable_gray",
                     d->enable_gray,
                     "Enable grayscale channel." );
  config->set_value( "enable_aligned_edge",
                     d->enable_aligned_edge,
                     "Enable aligned_edge_detection filter." );
  config->set_value( "enable_average",
                     d->enable_average,
                     "Enable average_frames filter." );
  config->set_value( "enable_color_commonality",
                     d->enable_color_commonality,
                     "Enable color_commonality_filter filter." );
  config->set_value( "enable_high_pass_box",
                     d->enable_high_pass_box,
                     "Enable high_pass_filter filter." );
  config->set_value( "enable_high_pass_bidir",
                     d->enable_high_pass_bidir,
                     "Enable high_pass_filter filter." );
  config->set_value( "enable_normalized_variance",
                     d->enable_normalized_variance,
                     "Enable the normalized variance since the last shot break. "
                     "This will be a scalar multiple with the normal variance until "
                     "shot breaks are implemented." );
  config->set_value( "enable_spatial_prior",
                     d->enable_spatial_prior,
                     "Enable an image which encodes the location" );
  config->set_value( "variance_scale_factor",
                     d->variance_scale_factor,
                     "The multiplicative value for the normalized varaince" );
  return config;
}

// ----------------------------------------------------------------------------
void
pixel_feature_extractor
::set_configuration( vital::config_block_sptr in_config )
{
  // Start with our generated vital::config_block to ensure that assumed values
  // are present. An alternative would be to check for key presence before
  // performing a get_value() call.
  vital::config_block_sptr config = this->get_configuration();
  config->merge_config( in_config );

  d->enable_color = config->get_value< bool >( "enable_color" );
  d->enable_gray = config->get_value< bool >( "enable_gray" );
  d->enable_aligned_edge = config->get_value< bool >( "enable_aligned_edge" );
  d->enable_average = config->get_value< bool >( "enable_average" );
  d->enable_color_commonality = config->get_value< bool >(
    "enable_color_commonality" );
  d->enable_high_pass_box =
    config->get_value< bool >( "enable_high_pass_box" );
  d->enable_high_pass_bidir =
    config->get_value< bool >( "enable_high_pass_bidir" );
  d->enable_normalized_variance =
    config->get_value< bool >( "enable_normalized_variance" );
  d->enable_spatial_prior =
    config->get_value< bool >( "enable_spatial_prior" );

  d->variance_scale_factor =
    config->get_value< float >( "variance_scale_factor" );

  // Configure the individual filter algorithms
  d->aligned_edge_detection_filter->set_configuration(
    config->subblock_view( "aligned_edge" ) );
  d->average_frames_filter->set_configuration(
    config->subblock_view( "average" ) );
  d->color_commonality_filter->set_configuration(
    config->subblock_view( "color_commonality" ) );
  d->high_pass_box_filter->set_configuration(
    config->subblock_view( "high_pass_box" ) );
  d->high_pass_bidir_filter->set_configuration(
    config->subblock_view( "high_pass_bidir" ) );
}

// ----------------------------------------------------------------------------
bool
pixel_feature_extractor
::check_configuration( vital::config_block_sptr config ) const
{
  auto enable_color = config->get_value< bool >( "enable_color" );
  auto enable_gray = config->get_value< bool >( "enable_gray" );
  auto enable_aligned_edge =
    config->get_value< bool >( "enable_aligned_edge" );
  auto enable_average = config->get_value< bool >( "enable_average" );
  auto enable_color_commonality = config->get_value< bool >(
    "enable_color_commonality" );
  auto enable_high_pass_box =
    config->get_value< bool >( "enable_high_pass_box" );
  auto enable_high_pass_bidir =
    config->get_value< bool >( "enable_high_pass_bidir" );
  auto enable_normalized_variance =
    config->get_value< bool >( "enable_normalized_variance" );
  auto enable_spatial_prior =
    config->get_value< bool >( "enable_spatial_prior" );

  if( !( enable_color || enable_gray || enable_aligned_edge ||
         enable_average || enable_color_commonality || enable_high_pass_box ||
         enable_high_pass_bidir || enable_normalized_variance ||
         enable_spatial_prior ) )
  {
    LOG_ERROR( logger(), "At least one filter must be enabled" );
    return false;
  }

  return d->check_sub_algorithm( config, "aligned_edge" ) &&
         d->check_sub_algorithm( config, "average" ) &&
         d->check_sub_algorithm( config, "color_commonality" ) &&
         d->check_sub_algorithm( config, "high_pass_box" ) &&
         d->check_sub_algorithm( config, "high_pass_bidir" );
}

// ----------------------------------------------------------------------------
kwiver::vital::image_container_sptr
pixel_feature_extractor
::filter( kwiver::vital::image_container_sptr image )
{
  // Perform Basic Validation
  if( !image )
  {
    LOG_ERROR( logger(), "Invalid image" );
    return kwiver::vital::image_container_sptr();
  }

  // Filter and with responses cast to bytes
  auto const responses = d->filter< vxl_byte >( image );

  return std::make_shared< vxl::image_container >(
    vxl::image_container{ responses } );
}

} // end namespace vxl

} // end namespace arrows

} // end namespace kwiver
