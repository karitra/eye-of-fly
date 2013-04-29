///
/// Changelog:
///  - Mon Apr 22 20:38:05 MSK 2013 hide 'bs' alias in methods
///
/// @todo
///
#include <boost/bind.hpp>

#include "streamer.h"

#include <Poco/Logger.h>
#include <Poco/Message.h>


#define  TO_BYTE(b)   static_cast<uint8_t  >(b)
#define  TO_PBYTE(b)  static_cast<uint8_t *>(b)

using Poco::Logger;
using Poco::Message;

// Note: tempory placeholeder!
#if 1
#define dbg(m) std::cerr << __func__ << ": " <<  m << std::endl
#else 
#define dbg(m)
#endif


namespace ba = boost::asio;
using namespace vid;


// inline void vid::operator>>(const vid::BinaryParser &rh, vid::JESStreamer::JESHdr &lh) 
// {
//   rh >> skip( JES_HDR_SIZE_OFFSET ) >> lh.size;
// }


class JPEGDecoder {
public:
  
};


vid::BinaryParser &
vid::operator>>(vid::BinaryParser &lh, const vid::skip &rh) 
{
  /// @todo
  //assert(lh.where + skip.to_skip <= lh.buffer + lh.len);

  lh.where += rh.to_skip;
  return lh;
}



void
vid::JESStreamer::start_reading( const ba::ip::tcp::socket::native_handle_type &so )
{
  insock.assign(ba::ip::tcp::v4(), so);
  return start_reading();
}

void
vid::JESStreamer::start_reading()
{
  uint8_t *data;
  size_t size;
  std::tie(data, std::ignore, std::ignore, size) = bootstrap;

  //size = 16;
  dbg("Start reading " << size << " bytes");

  insock.async_receive(ba::buffer( data, size), 
  					   strand.wrap(boost::bind(&JESStreamer::reading_handler_stub, 
  											   //shared_from_this(),
  											   this,   
  											   ba::placeholders::error,
  											   ba::placeholders::bytes_transferred)));
}

void
vid::JESStreamer::reading_handler_stub(const boost::system::error_code& er,
      size_t size)
{
  std::cerr << "Placeholder =>  get " << size << " bytes" << std::endl;
  
  /// @todo can we get 0 bytes?
  if (!size) {
	dbg("logical error: zero bytes recieved!");
	throw vid::exception::logic_error("get zero bytes");
  }

  /// @todo: error handig with posting to log
  if (er) {
	if (er == ba::error::eof) { // ok: remote have closed connection
	  dbg("connection closed by remote peer: " << er);
	} else {
	  dbg("error while reading data stream: " << er );
	}

	if (er == ba::error::operation_aborted) { // socket closed by ourselfs
	  dbg("reading cancel: " << er);
	}

	dbg("unknown error");
	return;
  }


  ToReadInPtr wp = std::make_pair(nullptr,0);

  // dbg("Choosing state");

  switch(state) {
  case State::InitState:
  case State::ReadJPEGFirstMarkState:
  case State::ReadJPEGSecondMarkState:
  case State::ReadJESFirstMarkState:
	wp = search_jpeg_mark(size);
	break;
  case State::ReadJESSecondMarkState:
  case State::ReadJESHeaderState:
	wp = load_jes_hdr(size);
	break;
  case State::GetJESHeaderState:
  case State::ReadJPEGFrameState:
	wp = load_jpeg(size);
	break;
  default:
	// TODO: throw exception and reinit state system
	dbg("unknown state while parsing incoming data: " << static_cast<unsigned>(state));
	break;
  };

  if (wp.first == nullptr) {
	dbg("logical error: wrong state " << static_cast<unsigned>(state));
	throw vid::exception::logic_error("internal error");
  }

  dbg("Reading async...");
  async_read(insock, 
			 // ba::buffer( std::get<field(BBufferField::RawData)>(bs) +
			 // 			 std::get<field(BBufferField::BytesWritten)>(bs),
			 // 			 to_write ),
			 ba::buffer( wp.first, wp.second),
			 ba::transfer_at_least(IO_CHUNK),
			 strand.wrap(boost::bind(&JESStreamer::reading_handler_stub, 
									 this,
									 //shared_from_this(),    ///< @todo why not working!?
									 ba::placeholders::error,
									 ba::placeholders::bytes_transferred)));
}

JESStreamer::pointer 
JESStreamer::make_streamer(ba::io_service &io, 
							 FramePool &mpool,
							 const ba::ip::tcp::socket::native_handle_type &so)
{
  JESStreamer *js = new JESStreamer(io, mpool, so);
  return pointer( dynamic_cast<Streamer*>(js) );
}

JESStreamer::pointer 
JESStreamer::make_streamer(CameraSPtr parent, ba::io_service &io, 
							 FramePool &mpool,
							 const ba::ip::tcp::socket::native_handle_type &so)
{
  //  return pointer( new JESStreamer(parent, io, mpool, so) );
  JESStreamer *js = new JESStreamer(parent, io, mpool, so);
  return pointer( dynamic_cast<Streamer*>(js) );
}

void 
JESStreamer::store_sock_holder(ISockSPtr so)
{
  this->pso = so;
}


void
JESStreamer::set_camera(CameraSPtr parent) 
{
  this->camera = parent;
}

CameraSPtr
JESStreamer::get_camera() 
{
  return camera.lock();
}


void
JESStreamer::stop_reading() 
{
  cancel = true;
  insock.cancel();
}

void
JESStreamer::close() 
{
  cancel = true;
  insock.close();
}




inline void
JESStreamer::set_hdr_pointer(uint8_t *hdr)
{
  std::get<field(BBufferField::JESHeader)>(bootstrap) = (nullptr != hdr) ? 
	hdr :
	std::get<field(BBufferField::RawData)>(bootstrap);
}

inline uint8_t *
JESStreamer::get_hdr_pointer()
{
  return std::get<field(BBufferField::JESHeader)>(bootstrap);
}

inline void 
JESStreamer::parse_jes_hdr() 
{
  jes_hdr.clear();
  
  assert( get_hdr_pointer() );

  // dbg("header ptr => " << (void *) get_hdr_pointer() );

  // Logger &d = Logger::get("dbg-connect");
  // d.dump("JES Header", get_hdr_pointer(), JES_HEADER_SIZE,
  // 		 Message::PRIO_ERROR );

  BinaryParser hdr(get_hdr_pointer(), JES_HEADER_SIZE);

  // dbg("data  ptr => " << (void *) hdr.data() );
  // dbg("where ptr => " << (void *) hdr.offset() );
  // dbg("jes size prior  => " << jes_hdr.size );

  hdr >> skip(JES_HDR_SIZE_OFFSET) >> jes_hdr.size;

  // dbg("jes size after  => " << std::dec << jes_hdr.size );
  // d.dump("Length part", hdr.offset()-4, sizeof(jes_hdr.size) ,
  // 		 Message::PRIO_ERROR );


  dbg("ready to read frame with size: " << std::dec << jes_hdr.size);
}

JESStreamer::State
JESStreamer::setInitState() 
{
  BootstrapBuffer &bs = bootstrap;

  // dbg("state init: " << (unsigned) state <<  " => " << 0 );

  state = State::InitState;
  std::get<field(BBufferField::JESHeader)>(bs) = nullptr;
  return state;
}


inline uint8_t *
JESStreamer::bootstrap_wrk_offset()
{
  return std::get<field(BBufferField::RawData)>(bootstrap) +
	std::get<field(BBufferField::BytesWritten)>(bootstrap);
}

inline size_t 
JESStreamer::reset_bootstrap() {
  std::get<field(BBufferField::BytesWritten)>(bootstrap) = 0;
  return std::get<field(BBufferField::Size)>(bootstrap);
}

inline size_t 
JESStreamer::update_bootstrap(uint8_t *p) 
{
  ptrdiff_t    diff = p - std::get<field(BBufferField::RawData)>(bootstrap);

  assert(diff <= std::get<field(BBufferField::Size)>(bootstrap) );

  // const size_t size =     std::get<field(BBufferField::Size)>(bootstrap);

  // if (diff > size ) {
  // 	diff = static_cast<ptrdiff_t>(size);
  // }

  std::get<field(BBufferField::BytesWritten)>(bootstrap) = static_cast<size_t>(diff);

  return diff;
}

inline size_t
JESStreamer::left_in_bootstrap() const
{
  return std::get<field(BBufferField::Size)>(bootstrap) - 
	std::get<field(BBufferField::BytesWritten)>(bootstrap);
}


inline size_t
JESStreamer::move_to_begin_bootstrap(uint8_t *buffer, const size_t left)
{
  memmove(std::get<field(BBufferField::RawData)>(bootstrap), // to
		  buffer, // from
		  left);  // count

  std::get<field(BBufferField::BytesWritten)>(bootstrap) = left;
  return std::get<field(BBufferField::Size)>(bootstrap) - left;
}

inline uint8_t *const
JESStreamer::bootstrap_begin() const 
{
  return std::get<field(BBufferField::RawData)>(bootstrap);
}

inline void 
JESStreamer::inc_bootstrap_written(const size_t size)
{
  std::get<field(BBufferField::BytesWritten)>(bootstrap) += size;
}

inline void 
JESStreamer::set_state(State new_state)
{
  // dbg("state transition: " << (unsigned) state <<  " => " << (unsigned) new_state); 

  state = new_state;
}


JESStreamer::ToReadInPtr 
JESStreamer::search_jpeg_mark(const size_t size) 
{
  using namespace std;

  BootstrapBuffer &bs = bootstrap;

  assert(size);

  // prepare working buffer alias
  uint8_t *buffer        = bootstrap_wrk_offset();
  uint8_t * const wrkwin = buffer;
  size_t left            = size;

  // 
  // 1. Search jpeg mark stamp: FF D8
  // 
  while(buffer < wrkwin + size) { // inside working window

	// dbg("State " << (unsigned) state);
	// dbg("window   size " << std::dec << (buffer - wrkwin) );
	// dbg("recieved size " << std::dec << size );
	// dbg("left     size " << std::dec << left );

	if (state == State::InitState) {
	  while(uint8_t *pm1 = TO_PBYTE(memchr(buffer, JPEGMARK1, left))) {
		if (pm1 < buffer + left - 1) { // some data left in buffer, check for second mark
		  
		  if (pm1[1] == TO_BYTE(JPEGMARK2)) { // found second mark
			set_state(State::ReadJPEGSecondMarkState);

			// adjust working window size
			// replace working buffer pointer
			left   -= pm1 + 2 - buffer;
			buffer  = pm1 + 2;     
			
			// dbg("left " << left);
			// dbg("pb1  " << hex << (unsigned) pm1[0] );
			// dbg("pb2  " << hex << (unsigned) pm1[1] );
			// dbg("pb3  " << hex << (unsigned) pm1[2] );
			// dbg("pb4  " << hex << (unsigned) pm1[3] );

			// jpeg mark was read we have done here, proceed to next iteration
			break;
		  } else { // false alert: second mark isn't of jpeg kind
			state = setInitState();

			// skip first misleading mark
			left  -= pm1 + 1 - buffer;
			buffer = pm1 + 1;

			continue;
		  }
		  
		} else {
		  // no more data in buffer to check, 
		  // try on next round
		  set_state(State::ReadJPEGFirstMarkState);

		  buffer += left; // just for logical consistency, not really needed
		  left    = 0;
		}
	  } // while getting first jpeg mark in frame window 	  

	  // No mark found in data, skip to next read iteration
	  // if state is 'init'
	  if (state == State::InitState) {
		buffer += left;
	    left    = 0;
		break;
	  }

	} // state == InitState

	if (state == State::ReadJPEGFirstMarkState && left) {
	  
	  state = buffer[0] == TO_BYTE(JPEGMARK2) ? 
		State::ReadJPEGSecondMarkState :
		setInitState();

	  buffer++;
	  left--;
	}

	if (state == State::ReadJPEGSecondMarkState && left) {

	  // dbg("byte0 => " << hex << (unsigned) buffer[-2]);
	  // dbg("byte1 => " << hex << (unsigned) buffer[-1]);
	  // dbg("byte2*=> " << hex << (unsigned) buffer[ 0]);
	  // dbg("byte3 => " << hex << (unsigned) buffer[ 1]);
	  // dbg("byte4 => " << hex << (unsigned) buffer[ 2]);

	  state = buffer[0] == TO_BYTE(JESMARK1) ? 
		State::ReadJESFirstMarkState :
		setInitState();

	  buffer++;
	  left--;
	}


	if (state == State::ReadJESFirstMarkState && left) {
	  
	  state = buffer[0] == TO_BYTE(JESMARK2) ? 
		State::ReadJESSecondMarkState :
		setInitState();

	  buffer++;
	  left--;

	  // if (state == State::ReadJESSecondMarkState ) {
	  // 	set_hdr_pointer(buffer);
	  // }
	}


	if (state == State::ReadJESSecondMarkState && left) {
	  // 'left' enough bytes in buffer for jes header

	  // dbg("loading jes header, left => " << dec << left);
	  // dbg("byte0 => " << hex << (unsigned) buffer[-4]);
	  // dbg("byte1 => " << hex << (unsigned) buffer[-3]);
	  // dbg("byte2 => " << hex << (unsigned) buffer[-2]);
	  // dbg("byte3 => " << hex << (unsigned) buffer[-1]);
	  // dbg("byte4*=> " << hex << (unsigned) buffer[ 0]);
	  // dbg("byte5 => " << hex << (unsigned) buffer[ 1]);
	  // dbg("byte6 => " << hex << (unsigned) buffer[ 2]);
	  // dbg("byte7 => " << hex << (unsigned) buffer[ 2]);

	  // dbg("buffer offset start   => " << (buffer - bootstrap_begin()));
	  // dbg("buffer offset working => " << (buffer - bootstrap_wrk_offset()));

	  update_bootstrap(buffer);

	  // dbg("buffer offset after => " <<  (buffer - bootstrap_wrk_offset()));

	  return load_jes_hdr(left);
	}

  } // while inside working window

  // update bytes written. it shouldn't overflows, because asio::buffer interface 
  // should insure requests in async operation bounds
  inc_bootstrap_written(size);

  size_t to_write = left_in_bootstrap();

  if (to_write <= BS_BUFFER_LOW_WATERMARK) {
	// reset buffer, if few bytes left
	to_write = reset_bootstrap();
  }

  return make_pair( bootstrap_wrk_offset(),
						 to_write);
}


JESStreamer::ToReadInPtr 
JESStreamer::load_jes_hdr(const size_t size) 
{
  using namespace std;

  assert(size);

  //BootstrapBuffer &bs = bootstrap;

  uint8_t *buffer        = bootstrap_wrk_offset();
  uint8_t * const wrkwin = buffer;
  size_t left            = size;

  switch(state) {
  case State::ReadJESSecondMarkState: 

	set_state(State::ReadJESHeaderState);

	//set_hdr_pointer(buffer);
	
	// all marks are checked at this point, we have a jpeg with jes;
	// chain it to header parser

	if (left >= JES_HEADER_SIZE) { // - JES_MARKS_NUM
	  // start to read jes header and we have place for whole header

	  set_state(State::GetJESHeaderState);

	  // current position - 4 byte marks (2 JPEG + 2 JES)
	  set_hdr_pointer(buffer-JES_MARKS_NUM);

	  // 'left' enough bytes in buffer for jes header
	  return load_jpeg(left);

	} else if (left_in_bootstrap() + left < JES_HEADER_SIZE + BS_BUFFER_LOW_WATERMARK) {

	  // no place in bootstrap to get whole header, move part to
	  // the start of bootstrap and init header pointer
	  size_t to_write = move_to_begin_bootstrap(buffer, left);
	  set_hdr_pointer(  bootstrap_begin() );

	  return make_pair( bootstrap_wrk_offset(), to_write);
	} else {
	  // we have enough place to read in, 
	  // but haven't enough bytes yet,
	  // just got to next read iteration in this state

	  update_bootstrap(buffer + left);

	  return make_pair(bootstrap_wrk_offset(),
					   left_in_bootstrap() );
	}

	break;
  case State::ReadJESHeaderState: // continue to read jes header
	// Note: fresh hdr pointer should be set at this point.
	// consider a bug otherwise
	if (buffer + left >= get_hdr_pointer() + JES_HEADER_SIZE) { // get it
	  set_state(State::GetJESHeaderState);
	  return load_jpeg(left);
	} else if (left_in_bootstrap() >= JES_HEADER_SIZE + BS_BUFFER_LOW_WATERMARK) { 
	  // need to continue on next iteration
	  update_bootstrap(buffer + left);
	  return make_pair(bootstrap_wrk_offset(),
					   left_in_bootstrap() );
	} else { 
	  // we don't get header on this round and don't have size to put it on next round
	  // move what we already got to beginning of the bootstrap and goto next turn
	  size_t to_write = move_to_begin_bootstrap(get_hdr_pointer(), 
												buffer + left - get_hdr_pointer());

	  set_hdr_pointer( bootstrap_begin() );
	  return make_pair( bootstrap_wrk_offset(), to_write);
	}

	break;
  };
}

JESStreamer::ToReadInPtr 
JESStreamer::load_jpeg(const size_t size) 
{
  using namespace std;

  assert(size);

  size_t left = size;

  // at this point we have full header (and pointer to it) 
  switch(state) {
  case State::GetJESHeaderState:
	parse_jes_hdr();
	// now we have incoming frame size we need to alloc it and set window pointers to it
	// and chain loading of data

	// for now start over
	reset_bootstrap();
	state = setInitState();
	return make_pair(bootstrap_wrk_offset(),
					 left_in_bootstrap());
	break;
  default:
  case State::ReadJPEGFrameState:
	//
	// Continue loading of data until all data read
	// 

	//
	// Get all frame 
	//
	
	//
	// Decompress image
	//

	//
	// Store it in circular buffer (possibly deleting old one)
	//

	//
	// Reset global state
	//
	reset_bootstrap();
	state = setInitState();
	return make_pair(bootstrap_wrk_offset(),
					 left_in_bootstrap());

	break;
  };
}

