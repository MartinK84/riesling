#include "types.hpp"

#include "io/hd5.hpp"
#include "log.hpp"
#include "op/pad.hpp"
#include "parse_args.hpp"
#include "threads.hpp"

using namespace rl;

void main_pad(args::Subparser &parser)
{
  args::Positional<std::string>      iname(parser, "FILE", "Input HD5 file");
  args::Positional<std::string>      oname(parser, "FILE", "Output HD5 file");
  args::Positional<Sz3, SzReader<3>> padDims(parser, "SZ", "Pad/crop dimensions");
  args::Flag                         fwd(parser, "", "Apply forward operation", {'f', "fwd"});
  args::Flag                         channels(parser, "C", "Work on channels, not images", {'c', "channels"});
  ParseCommand(parser, iname);
  HD5::Reader reader(iname.Get());
  HD5::Writer writer(oname.Get());
  writer.writeInfo(reader.readInfo());
  // writer.writeTrajectory(reader.trajectory());
  if (channels) {
    Cx6          inImages = reader.readTensor<Cx6>();
    Sz6          inDims = inImages.dimensions();
    Cx6          outImages(Sz6{inDims[0], inDims[1], padDims.Get()[0], padDims.Get()[1], padDims.Get()[2], inDims[5]});
    auto const   start = Log::Now();
    TOps::Pad<Cx, 5> pad(LastN<3>(inDims), padDims.Get(), FirstN<2>(inDims));
    if (fwd) {
      for (Index ii = 0; ii < inDims[5]; ii++) {
        outImages.chip(ii, 5) = pad.forward(CChipMap(inImages, ii));
      }
      Log::Print("Pad took {}", Log::ToNow(start));
    } else {
      for (Index ii = 0; ii < inDims[5]; ii++) {
        outImages.chip(ii, 5) = pad.adjoint(CChipMap(inImages, ii));
      }
      Log::Print("Pad Adjoint took {}", Log::ToNow(start));
    }
    writer.writeTensor(HD5::Keys::Data, outImages.dimensions(), outImages.data());
  } else {
    Cx5         inImages = reader.readTensor<Cx5>();
    Sz5         inDims = inImages.dimensions();
    Index const nF = inDims[0];
    Index const nV = inDims[4];
    Cx5         outImages(Sz5{nF, padDims.Get()[0], padDims.Get()[1], padDims.Get()[2], nV});
    auto const  start = Log::Now();
    if (fwd) {
      TOps::Pad<Cx, 4> pad(MidN<1, 3>(inDims), padDims.Get(), Sz1{nF});
      for (Index ii = 0; ii < inDims[4]; ii++) {
        Cx4 img = inImages.chip(ii, 4);
        outImages.chip(ii, 4) = pad.forward(img);
      }
      Log::Print("Pad took {}", Log::ToNow(start));
    } else {
      TOps::Pad<Cx, 4> pad(padDims.Get(), MidN<1, 3>(inDims), Sz1{nF});
      for (Index ii = 0; ii < inDims[4]; ii++) {
        Cx4 img = inImages.chip(ii, 4);
        outImages.chip(ii, 4) = pad.adjoint(img);
      }
      Log::Print("Pad Adjoint took {}", Log::ToNow(start));
    }
    writer.writeTensor(HD5::Keys::Data, outImages.dimensions(), outImages.data());
  }}
