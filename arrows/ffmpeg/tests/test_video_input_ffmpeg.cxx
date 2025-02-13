// This file is part of KWIVER, and is distributed under the
// OSI-approved BSD 3-Clause License. See top-level LICENSE file or
// https://github.com/Kitware/kwiver/blob/master/LICENSE for details.

/// \file
/// \brief test opening/closing a video file

#include <test_gtest.h>

#include <arrows/core/video_input_filter.h>
#include <arrows/ffmpeg/ffmpeg_video_input.h>
#include <arrows/tests/test_video_input.h>
#include <vital/exceptions/io.h>
#include <vital/exceptions/video.h>
#include <vital/plugin_loader/plugin_manager.h>

#include <iostream>
#include <memory>
#include <string>

kwiver::vital::path_t g_data_dir;

namespace algo = kwiver::vital::algo;
namespace ffmpeg = kwiver::arrows::ffmpeg;
namespace kv = kwiver::vital;

static int TOTAL_NUMBER_OF_FRAMES = 50;
static std::string video_name = "videos/ffmpeg_video.mp4";
static std::string short_video_name = "videos/aphill_short.ts";

// ----------------------------------------------------------------------------
int
main( int argc, char* argv[] )
{
  ::testing::InitGoogleTest( &argc, argv );
  TEST_LOAD_PLUGINS();

  GET_ARG( 1, g_data_dir );

  return RUN_ALL_TESTS();
}

// ----------------------------------------------------------------------------
class ffmpeg_video_input : public ::testing::Test
{
  TEST_ARG( data_dir );
};

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, create )
{
  EXPECT_NE( nullptr, algo::video_input::create( "ffmpeg" ) );
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, is_good_correct_file_path )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;
  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  EXPECT_FALSE( input.good() )
    << "Video state before open";

  // open the video
  input.open( correct_file );
  EXPECT_FALSE( input.good() ) <<
    "Video state after open but before first frame";

  // Get the next frame
  kwiver::vital::timestamp ts;
  EXPECT_TRUE( input.next_frame( ts ) ) <<
    "Video state after open but before first frame";
  EXPECT_EQ( ts.get_frame(), 1 ) << "Initial frame value mismastch";
  EXPECT_TRUE( input.good() ) <<
    "Video state after open but before first frame";

  // close the video
  input.close();
  EXPECT_FALSE( input.good() ) <<
    "Video state after close";
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, is_good_invalid_file_path )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;
  kwiver::vital::path_t incorrect_file = data_dir + "/DoesNOTExists.mp4";

  EXPECT_FALSE( input.good() ) <<
    "Video state before open";

  // open the video
  EXPECT_THROW(
    input.open( incorrect_file ),
    kwiver::vital::file_not_found_exception );
  EXPECT_FALSE( input.good() ) <<
    "Video state after open but before first frame";

  // Get the next frame
  kwiver::vital::timestamp ts;
  EXPECT_THROW( input.next_frame( ts ),
                kwiver::vital::file_not_read_exception );
  EXPECT_EQ( ts.get_frame(), 0 ) << "Initial frame value mismastch";
  EXPECT_FALSE( input.good() ) <<
    "Video state after open but before first frame";

  // close the video
  input.close();
  EXPECT_FALSE( input.good() ) <<
    "Video state after close";
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, frame_image )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;
  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  EXPECT_FALSE( input.good() ) <<
    "Video state before open";

  // open the video
  input.open( correct_file );
  EXPECT_FALSE( input.good() ) <<
    "Video state after open but before first frame";
  EXPECT_EQ( input.frame_image(),
             nullptr ) << "Video should not have an image yet";

  // Get the next frame
  kwiver::vital::timestamp ts;
  input.next_frame( ts );
  EXPECT_EQ( ts.get_frame(), 1 );

  kwiver::vital::image_container_sptr frame = input.frame_image();
  EXPECT_EQ( frame->depth(), 3 );
  EXPECT_EQ( frame->get_image().width(), 80 );
  EXPECT_EQ( frame->get_image().height(), 54 );
  EXPECT_EQ( frame->get_image().d_step(), 1 );
  EXPECT_EQ( frame->get_image().h_step(), 80 * 3 );
  EXPECT_EQ( frame->get_image().w_step(), 3 );
  EXPECT_TRUE( frame->get_image().is_contiguous() );

  EXPECT_EQ( decode_barcode( *frame ), 1 );
}

// ----------------------------------------------------------------------------
// Verify that disabling imagery processing acts as expected and doesn't break
// anything else.
TEST_F( ffmpeg_video_input, imagery_disabled )
{
  ffmpeg::ffmpeg_video_input input;
  auto const filename = data_dir + "/" + short_video_name;

  auto config = input.get_configuration();
  config->set_value< bool >( "imagery_enabled", false );
  input.set_configuration( config );
  input.open( filename );

  EXPECT_FALSE( input.good() );
  EXPECT_EQ( input.frame_image(), nullptr );

  kv::frame_id_t frame_count = 0;
  kv::timestamp ts;
  while( input.next_frame( ts ) )
  {
    ++frame_count;
    EXPECT_TRUE( input.good() );
    EXPECT_EQ( input.frame_image(), nullptr );
    EXPECT_EQ( ts.get_frame(), frame_count );

    auto const md = input.frame_metadata();
    ASSERT_FALSE( md.empty() );
    ASSERT_TRUE( md.at( 0 )->has( kv::VITAL_META_UNIX_TIMESTAMP ) );
  }

  input.close();
  EXPECT_FALSE( input.good() );
}

// ----------------------------------------------------------------------------
// Verify that disabling KLV processing acts as expected and doesn't break
// anything else.
TEST_F( ffmpeg_video_input, klv_disabled )
{
  ffmpeg::ffmpeg_video_input input;
  auto const filename = data_dir + "/" + video_name;

  auto config = input.get_configuration();
  config->set_value< bool >( "klv_enabled", false );
  input.set_configuration( config );
  input.open( filename );

  EXPECT_FALSE( input.good() );
  EXPECT_FALSE(
    input.get_implementation_capabilities().capability(
      algo::video_input::HAS_METADATA ) );

  kv::frame_id_t frame_count = 0;
  kv::timestamp ts;
  while( input.next_frame( ts ) )
  {
    ++frame_count;
    EXPECT_TRUE( input.good() );
    EXPECT_NE( input.frame_image(), nullptr );
    EXPECT_EQ( ts.get_frame(), frame_count );

    auto const md = input.frame_metadata();
    ASSERT_FALSE( md.empty() );
    ASSERT_FALSE( md.at( 0 )->has( kv::VITAL_META_UNIX_TIMESTAMP ) );
  }

  input.close();
  EXPECT_FALSE( input.good() );
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, seek_frame )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // open the video
  input.open( correct_file );

  test_seek_frame( input );

  input.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, seek_then_next_frame )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // open the video
  input.open( correct_file );

  test_seek_then_next( input );

  input.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, next_then_seek_frame )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // open the video
  input.open( correct_file );

  test_next_then_seek( input );

  input.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, next_then_seek_then_next )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // open the video
  input.open( correct_file );

  test_next_then_seek_then_next( input );

  input.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, end_of_video )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  EXPECT_TRUE( input.end_of_video() ) <<
    "End of video before open";

  // open the video
  input.open( correct_file );
  EXPECT_FALSE( input.end_of_video() ) <<
    "End of video after open";

  kwiver::vital::timestamp ts;
  while( input.next_frame( ts ) )
  {
    EXPECT_FALSE( input.end_of_video() ) << "End of video while reading";
  }

  EXPECT_EQ( ts.get_frame(), TOTAL_NUMBER_OF_FRAMES ) << "Last frame";
  EXPECT_TRUE( input.end_of_video() ) << "End of video after last frame";
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, read_video_aphill )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + short_video_name;

  input.open( correct_file );

  kwiver::vital::timestamp ts;

  int num_frames = 0;
  while( input.next_frame( ts ) )
  {
    ++num_frames;
    EXPECT_EQ( num_frames, ts.get_frame() ) <<
      "Frame numbers should be sequential";
  }

  input.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, read_video )
{
  // make config block
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  input.open( correct_file );

  kwiver::vital::timestamp ts;

  EXPECT_EQ( TOTAL_NUMBER_OF_FRAMES, input.num_frames() ) <<
    "Number of frames before extracting frames should be " <<
    TOTAL_NUMBER_OF_FRAMES;

  int num_frames = 0;
  while( input.next_frame( ts ) )
  {
    auto img = input.frame_image();
    auto md = input.frame_metadata();

    ++num_frames;
    EXPECT_EQ( num_frames, ts.get_frame() ) <<
      "Frame numbers should be sequential";
    EXPECT_EQ( ts.get_frame(), decode_barcode( *img ) ) <<
      "Frame number should match barcode in frame image";

    EXPECT_EQ( red, test_color_pixel( 1, *img ) );
    EXPECT_EQ( green, test_color_pixel( 2, *img ) );
    EXPECT_EQ( blue, test_color_pixel( 3, *img ) );
  }
  EXPECT_EQ( TOTAL_NUMBER_OF_FRAMES, num_frames ) <<
    "Number of frames found should be " <<
    TOTAL_NUMBER_OF_FRAMES;
  EXPECT_EQ( TOTAL_NUMBER_OF_FRAMES, input.num_frames() ) <<
    "Number of frames after extracting frames should be " <<
    TOTAL_NUMBER_OF_FRAMES;
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, read_video_nth_frame_output )
{
  // Make config block
  auto config = kwiver::vital::config_block::empty_config();

  config->set_value( "video_input:type", "ffmpeg" );
  config->set_value( "output_nth_frame", nth_frame_output );

  kwiver::arrows::core::video_input_filter vif;

  EXPECT_TRUE( vif.check_configuration( config ) );
  vif.set_configuration( config );

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( correct_file );

  test_read_video_nth_frame( vif );

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, seek_nth_frame_output )
{
  // Make config block
  auto config = kwiver::vital::config_block::empty_config();

  config->set_value( "video_input:type", "ffmpeg" );
  config->set_value( "output_nth_frame", nth_frame_output );

  kwiver::arrows::core::video_input_filter vif;

  EXPECT_TRUE( vif.check_configuration( config ) );
  vif.set_configuration( config );

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( correct_file );

  test_seek_nth_frame( vif );

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, read_video_sublist )
{
  // Make config block
  auto config = kwiver::vital::config_block::empty_config();

  config->set_value( "video_input:type", "ffmpeg" );
  config->set_value( "start_at_frame", start_at_frame );
  config->set_value( "stop_after_frame", stop_after_frame );

  kwiver::arrows::core::video_input_filter vif;

  EXPECT_TRUE( vif.check_configuration( config ) );
  vif.set_configuration( config );

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( correct_file );

  test_seek_frame_sublist( vif );

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, read_video_sublist_nth_frame )
{
  // Make config block
  auto config = kwiver::vital::config_block::empty_config();

  config->set_value( "video_input:type", "ffmpeg" );
  config->set_value( "start_at_frame", start_at_frame );
  config->set_value( "stop_after_frame", stop_after_frame );
  config->set_value( "output_nth_frame", nth_frame_output );

  kwiver::arrows::core::video_input_filter vif;

  EXPECT_TRUE( vif.check_configuration( config ) );
  vif.set_configuration( config );

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( correct_file );

  test_read_video_sublist_nth_frame( vif );

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, seek_frame_sublist_nth_frame )
{
  // Make config block
  auto config = kwiver::vital::config_block::empty_config();

  config->set_value( "video_input:type", "ffmpeg" );
  config->set_value( "start_at_frame", start_at_frame );
  config->set_value( "stop_after_frame", stop_after_frame );
  config->set_value( "output_nth_frame", nth_frame_output );

  kwiver::arrows::core::video_input_filter vif;

  EXPECT_TRUE( vif.check_configuration( config ) );
  vif.set_configuration( config );

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( correct_file );

  test_seek_sublist_nth_frame( vif );

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, metadata_map )
{
  // make config block
  kwiver::arrows::ffmpeg::ffmpeg_video_input input;

  kwiver::vital::path_t correct_file = data_dir + "/" + video_name;

  input.open( correct_file );

  // metadata caapbility is false since no external metadata is present
  auto const& caps = input.get_implementation_capabilities();
  EXPECT_FALSE( caps.capability(
                  kwiver::vital::algo::video_input::HAS_METADATA ) );

  // Get metadata map
  auto md_map = input.metadata_map()->metadata();

  // Each frame of video should have some metadata
  // at a minimum this is just the video name and timestamp
  EXPECT_EQ( md_map.size(), input.num_frames() );

  if( md_map.size() != input.num_frames() )
  {
    std::cout << "Found metadata on these frames: ";
    for( auto md : md_map )
    {
      std::cout << md.first << ", ";
    }

    std::cout << std::endl;
  }
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, sync_metadata )
{
  std::map< int, std::set< uint64_t > > expected_md =
  { { 0, { 1221515219356000, 1221515219396000 } },
    { 1, { 1221515219426000 } },
    { 2, { 1221515219456000 } },
    { 3, { 1221515219486000 } },
    { 4, { 1221515219516000 } } };

  kwiver::arrows::ffmpeg::ffmpeg_video_input vif;

  auto config = vif.get_configuration();
  vif.set_configuration( config );

  kwiver::vital::path_t video_path = data_dir + "/" + short_video_name;

  // Open the video
  vif.open( video_path );

  auto const& caps = vif.get_implementation_capabilities();
  EXPECT_TRUE( caps.capability(
                 kwiver::vital::algo::video_input::HAS_METADATA ) );

  kwiver::vital::timestamp ts;

  unsigned int frame_num = 0;
  while( vif.next_frame( ts ) )
  {
    auto md_vect = vif.frame_metadata();

    EXPECT_TRUE( md_vect.size() > 0 ) <<
      "Each frame tested should have metadata present";

    for( auto md : md_vect )
    {
      EXPECT_TRUE( md->has( kwiver::vital::VITAL_META_UNIX_TIMESTAMP ) ) <<
        "Each of the first five frames should have a UNIX time stamp in" <<
        " its metadata";

      for( auto md_item : *md )
      {
        if( md_item.first == kwiver::vital::VITAL_META_UNIX_TIMESTAMP )
        {
          EXPECT_TRUE( expected_md[ frame_num ].find(
                         md_item.second->as_uint64() ) !=
                       expected_md[ frame_num ].end() ) <<
            "UNIX time stamp " << md_item.second->as_uint64() <<
            " was not found in metadata for frame " << frame_num;
        }
      }
    }

    frame_num++;

    if( frame_num >= expected_md.size() )
    {
      break;
    }
  }

  vif.close();
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, empty_filter_desc )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input vif;
  auto config = vif.get_configuration();
  // make the avfilter pipeline empty
  config->set_value( "filter_desc", "" );
  vif.set_configuration( config );

  kwiver::vital::path_t video_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( video_file );

  // Get the next frame
  kwiver::vital::timestamp ts;
  vif.next_frame( ts );
  EXPECT_EQ( ts.get_frame(), 1 );

  kwiver::vital::image_container_sptr frame = vif.frame_image();
  EXPECT_EQ( frame->depth(), 3 );
  EXPECT_EQ( frame->get_image().width(), 80 );
  EXPECT_EQ( frame->get_image().height(), 54 );
  EXPECT_EQ( frame->get_image().d_step(), 1 );
  EXPECT_EQ( frame->get_image().h_step(), 80 * 3 );
  EXPECT_EQ( frame->get_image().w_step(), 3 );
  EXPECT_TRUE( frame->get_image().is_contiguous() );

  EXPECT_EQ( decode_barcode( *frame ), 1 );

  vif.next_frame( ts );
  frame = vif.frame_image();
  EXPECT_EQ( ts.get_frame(), 2 );
  EXPECT_EQ( decode_barcode( *frame ), 2 );
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, invalid_filter_desc )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input vif;
  auto config = vif.get_configuration();
  // set an invalid avfilter pipeline in the filter description
  config->set_value( "filter_desc", "_invalid_filter_" );
  vif.set_configuration( config );

  kwiver::vital::path_t video_file = data_dir + "/" + video_name;

  // Open the video
  EXPECT_THROW(
    vif.open( video_file ),
    kwiver::vital::video_runtime_exception );
}

// ----------------------------------------------------------------------------
// helper function to make a horizontally flipped image view
// TODO: make this a more general function within KWIVER
kwiver::vital::image
hflip_image( kwiver::vital::image const& image )
{
  const auto w = image.width();
  const auto h = image.height();
  const auto d = image.depth();
  const auto ws = image.w_step();
  const auto hs = image.h_step();
  const auto ds = image.d_step();
  return kwiver::vital::image(
    image.memory(),
    static_cast< const uint8_t* >( image.first_pixel() ) + ws * ( w - 1 ),
    w, h, d, -ws, hs + ws * w, ds, image.pixel_traits() );
}

// ----------------------------------------------------------------------------
TEST_F ( ffmpeg_video_input, hflip_filter_desc )
{
  kwiver::arrows::ffmpeg::ffmpeg_video_input vif;
  auto config = vif.get_configuration();
  // use the hflip filter for horizontal flipping
  config->set_value( "filter_desc", "hflip" );
  vif.set_configuration( config );

  kwiver::vital::path_t video_file = data_dir + "/" + video_name;

  // Open the video
  vif.open( video_file );

  // Get the next frame
  kwiver::vital::timestamp ts;
  vif.next_frame( ts );
  EXPECT_EQ( ts.get_frame(), 1 );

  kwiver::vital::image_container_sptr frame = vif.frame_image();
  EXPECT_EQ( frame->depth(), 3 );
  EXPECT_EQ( frame->get_image().width(), 80 );
  EXPECT_EQ( frame->get_image().height(), 54 );
  EXPECT_EQ( frame->get_image().d_step(), 1 );
  EXPECT_EQ( frame->get_image().h_step(), 80 * 3 );
  EXPECT_EQ( frame->get_image().w_step(), 3 );
  EXPECT_TRUE( frame->get_image().is_contiguous() );

  EXPECT_NE( decode_barcode( *frame ), 1 );

  // undo horizontal flipping and confirm that the frame is now correct
  kwiver::vital::simple_image_container hflip_frame( hflip_image(
                                                       frame->get_image() ) );
  EXPECT_EQ( decode_barcode( hflip_frame ), 1 );

  vif.next_frame( ts );
  frame = vif.frame_image();
  EXPECT_EQ( ts.get_frame(), 2 );
  EXPECT_NE( decode_barcode( *frame ), 2 );

  // undo horizontal flipping and confirm that the frame is now correct
  hflip_frame =
    kwiver::vital::simple_image_container( hflip_image( frame->get_image() ) );
  EXPECT_EQ( decode_barcode( hflip_frame ), 2 );
}
