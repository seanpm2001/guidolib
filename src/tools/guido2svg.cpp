
#ifndef WIN32
#include <libgen.h>
#include <unistd.h>
#else
#include <io.h>
#pragma warning(disable:4996)
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdlib.h>

#include "SVGSystem.h"
#include "SVGDevice.h"
#include "SVGFont.h"

#include "GUIDOParse.h"
#include "GUIDOEngine.h"
#include "GUIDOScoreMap.h"

//------------------------------------------------------------------------------------------
// Version 1.00 (2014-12-08)
//------------------------------------------------------------------------------------------

using namespace std;

static void usage (char* name)
{
#ifndef WIN32
	const char* tool = basename (name);
#else
	const char* tool = name;
#endif
	cerr << "usage: " << tool << " [options] <gmn file>" << endl;
	cerr << "       convert GMN code to svg" << endl;
	cerr << "       options:" << endl;
	cerr << "           	-o <file>             : output file name" << endl;
	cerr << "           	-f --fontfile <file>  : include the font taken from file" << endl;
	cerr << "           	   --font     <name>  : use the 'name' as musical font" << endl;
	cerr << "           	-p pagenum            : an optional page number (default is 1)" << endl;
	cerr << "       reads the standard input when gmn file is omitted." << endl;
	cerr << "           	-voicemap  boolean : enables or not event mapping draw (default is false)" << endl;
    cerr << "           	-staffmap  boolean : enables or not staff mapping draw (default is false)" << endl;
    cerr << "           	-systemmap boolean : enables or not system mapping draw (default is false)" << endl;
    cerr << "           	-checkLyrics       : enables lyrics collisions detection" << endl;
    cerr << "           	-optimalPageFill   : enables optimal page filling" << endl;
    cerr << "           	-viewport          : add viewport information (in addition to viewBox)" << endl;
	exit(1);
}

static void error (GuidoErrCode err)
{
	cerr << "error #" << err << ": " << GuidoGetErrorString (err) << endl;
	exit(1);
}


//--------------------------------------------------------------------------
// utility for command line arguments 
//--------------------------------------------------------------------------
static int getIntOption (int argc, char *argv[], const std::string& option, int defaultValue)
{
	for (int i = 1; i < argc - 1; i++) {
		if (option == argv[i]) {
			int val = strtol(argv[i + 1], 0, 10);
			if (val)
                return val;
		}
	}

	return defaultValue;
}

static int getBoolOption (int argc, char *argv[], const std::string& option, int defaultValue)
{
	for (int i = 1; i < argc - 1; i++) {
		if (option == argv[i]) {
            if (!strcmp(argv[i + 1], "true"))
			    return true;
            else
                return false;
		}
	}

	return defaultValue;
}

static int getBoolOption (int argc, char *argv[], const std::string& option)
{
	for (int i = 1; i < argc - 1; i++) {
		if (option == argv[i]) return true;
	}
	return false;
}

static const char* getOption (int argc, char *argv[], const std::string& option, const char* defaultValue)
{
	for (int i = 1; i < argc - 1; i++) {
		if (option == argv[i])
			return argv[i + 1];
	}

	return defaultValue;
}

static const char* getFile (int argc, char *argv[])
{
	return argv[argc-1];
}

#ifdef WIN32
const char pathSep = '\\';
#else
const char pathSep = '/';
#endif
//--------------------------------------------------------------------------
// utility for making correspondance between ar and directory path
//--------------------------------------------------------------------------
vector<string> fillPathsVector (const char *filename)
{
    vector<string> pathsVector;
	pathsVector.push_back(".");			// look first for external rsrc in the current folder
	if (filename) {
		string s(filename);
		size_t n = s.find_last_of (pathSep);
		if (n != string::npos) {		// add also the gmn file path
//			cerr << "fillPathsVector add " << s.substr(0,n) << endl;
			pathsVector.push_back(s.substr(0,n));
		}
	}

#ifdef WIN32
    // For windows
    string homePath(getenv("HOMEDRIVE"));
    homePath.append(getenv("HOMEPATH"));
#else
    // For unix
    string homePath(getenv("HOME"));
#endif
    pathsVector.push_back(homePath);	// and add the HOME directory

    return pathsVector;
}

//_______________________________________________________________________________
static void check (int argc, char *argv[])
{
	if (argc < 2) usage(argv[0]);
	for (int i = 1; i < argc; i++) {
		const char* ptr = argv[i];
		if (*ptr++ == '-') {
			if ((*ptr != 'p') && (*ptr != 'f') && (*ptr != 'o')
				&& strcmp(ptr, "-fontfile") && strcmp(ptr, "-font")
				&& strcmp(ptr, "staffmap") && strcmp(ptr, "voicemap")
				&& strcmp(ptr, "systemmap") && strcmp(ptr, "checkLyrics")
				&& strcmp(ptr, "viewport"))
                usage(argv[0]);
		}
	}
}

//_______________________________________________________________________________
static bool readfile (FILE * fd, string& content) 
{
	if (!fd)
        return false;

	do {
		int c = getc(fd);
		if (feof(fd) || ferror(fd)) break;
		content += c;
	} while (true);

	return ferror(fd) == 0;
}

//_______________________________________________________________________________
static GuidoErrCode Draw( VGDevice* dev, const GRHandler handle, int page, std::ostream& out)
{
	GuidoOnDrawDesc desc;              // declare a data structure for drawing
	desc.handle = handle;

	GuidoPageFormat	pf;
	GuidoResizePageToMusic (handle);
	GuidoGetPageFormat (handle, page, &pf);

	desc.hdc = dev;                    // we'll draw on the svg device
	desc.page = page;
	desc.updateRegion.erase = true;     // and draw everything
	desc.scrollx = desc.scrolly = 0;    // from the upper left page corner
	desc.sizex = int(pf.width/SVGDevice::kSVGSizeDivider);
	desc.sizey = int(pf.height/SVGDevice::kSVGSizeDivider);
	dev->NotifySize(desc.sizex, desc.sizey);
	dev->SelectPenColor(VGColor(0,0,0));

	return GuidoOnDraw (&desc);
}

//_______________________________________________________________________________
int main(int argc, char **argv)
{
	check (argc, argv);
	int page = getIntOption (argc, argv, "-p", 1);

	if (page < 0) {
		cerr << "page number should be positive" << endl;
		usage(argv[0]);
	}

	const char* musicfont 	 = getOption (argc, argv, "--font", 0);
	const char* outfile = getOption (argc, argv, "-o", 0);
	const char* fontfile = getOption (argc, argv, "-f", 0);
	if (!fontfile) fontfile = getOption (argc, argv, "--fontfile", 0);
	const char* filename = getFile (argc, argv);

	string gmn;							// a string to read the standard input
	if (!filename)						// std in mode
		readfile (stdin, gmn);

	SVGSystem sys(fontfile);
	VGDevice *dev = sys.CreateDisplayDevice();
    GuidoInitDesc gd = { dev, 0, musicfont, 0 };
    GuidoInit(&gd);                    // Initialise the Guido Engine first
	
	GuidoErrCode err;
    ARHandler arh;

    /* For symbol-tag */
    vector<string> pathsVector = fillPathsVector(filename);
    GuidoParser *parser = GuidoOpenParser();
	if (gmn.size())
		arh = GuidoString2AR(parser, gmn.c_str());
	else {
        std::ifstream ifs(filename, ios::in);
        if (!ifs)  return 0;
        std::stringstream streamBuffer;
        streamBuffer << ifs.rdbuf();
        ifs.close();

		arh = GuidoString2AR(parser, streamBuffer.str().c_str());
    }
	if (!arh) {
		int line, col;
		const char* msg;
		err = GuidoParserGetErrorCode (parser, line, col, &msg);
		cerr << "line " << line << " column " << col << ": " << msg << endl;
		exit(1);
	}

    /* For symbol-tag */
    GuidoSetSymbolPath(arh, pathsVector);
    /******************/
    GRHandler grh;
	GuidoLayoutSettings settings;
	GuidoGetDefaultLayoutSettings (&settings);
	settings.checkLyricsCollisions 	= getBoolOption(argc, argv, "-checkLyrics");
	settings.optimalPageFill 		= getBoolOption(argc, argv, "-optimalPageFill");
	err = GuidoAR2GR (arh, &settings, &grh);
    if (err != guidoNoErr)
        error(err);

    /**** MAPS ****/
    int mappingMode = kNoMapping;

    if (getBoolOption(argc, argv, "-voicemap", false))
        mappingMode += kVoiceMapping;

    if (getBoolOption(argc, argv, "-staffmap", false))
        mappingMode += kStaffMapping;

    if (getBoolOption(argc, argv, "-systemmap", false))
        mappingMode += kSystemMapping;
    /*************/

	bool viewPort = getBoolOption(argc, argv, "-viewport");
	if (viewPort && mappingMode) cerr << "Warning: viewport not supported with mappings" << endl;

	ostream* out;
	if (outfile)
		out = new ofstream (outfile);
	else out = &cout;

	VGDevice *svg = 0;
	if (mappingMode)
		svg = sys.CreateDisplayDevice(*out, mappingMode);
	else
		svg = new SVGDevice(*out, &sys, fontfile, viewPort);
	
	err = Draw( svg, grh, page, *out);
    if (err != guidoNoErr)
        error(err);

	delete svg;
    GuidoCloseParser(parser);
	return 0;
}


