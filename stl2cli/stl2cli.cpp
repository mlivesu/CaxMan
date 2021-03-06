#include "imatistl.h"
#include "caxlib/io/read_ANN.h"
#include "caxlib/io/read_ZIP.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>

using namespace IMATI_STL;

// A segment belonging to a piecewise linear chain with constant 'z' coordinate
// Each of the points is generated by the intersection of a mesh edge with a slicing plane parallel to X-Y.
// Such 'generating' edge is stored in the info pointer of the points.

class segment {
	Point p1, p2;			// The end-points
	class segment *n1, *n2;	// The neighboring segments
	bool mask;				// A flag to mark visits

public:
	segment(Point *a, Point *b, Edge *e1, Edge *e2) : p1(a), p2(b) { p1.info = e1; p2.info = e2; mask = false; n1 = n2 = NULL; }

	const Point *getP1() const { return &p1; }
	const Point *getP2() const { return &p2; }
	segment *getNextSegment() const { return n2; }
	segment *getPrevSegment() const { return n1; }
	segment *oppositeNeighbor(const segment *p) const { return ((p == n1) ? (n2) : (n1)); }
	bool isMarked() const { return mask; }

	// The 'height' of the segment is the 'Z' coordinate of any of its points
	coord getHeight() const { return p1.z; }

	// This establishes the mutual connectivity between this segment and 'a'.
	// The generating mesh edges are used to decide the correct orientation.
	bool linkWith(segment *a)
	{
		if (a->p2.info == p1.info)
		{
			if (n1 == NULL && a->n2 == NULL) { n1 = a; a->n2 = this; }
			else TMesh::error("can't add neighbor. should not happen!\n");
		}
		else if (a->p1.info == p2.info)
		{
			if (n2 == NULL && a->n1 == NULL) { n2 = a; a->n1 = this; }
			else TMesh::error("can't add neighbor. should not happen!\n");
		}
		else TMesh::error("can't add neighbor. segments do not share a point!\n");

		return true;
	}
	
	// If the chain os open, returns the first segment of the chain.
	// If it is closed, return this segment.
	segment *getFirstSegmentInCurve()
	{
		segment *s = this;
		while (s->n1 != NULL) {	s = s->n1; if (s == this) return s;	}
		return s;
	}

	// Mark this segment and its connected component as visited
	void markAndPropagate()
	{
		segment *s = this;
		for (s = this; s != NULL; s = (s->n2 != this) ? (s->n2) : (NULL)) s->mask = true;
		for (s = this; s != NULL; s = (s->n1 != this) ? (s->n1) : (NULL)) s->mask = true;
	}

	// To sort segments by height
	static bool compSegPtr(segment * a, segment * b) { return a->getHeight() < b->getHeight(); }
};


// This represents a set of chains
typedef std::vector<segment *> c_slice;


// The set of segments generated by the intersection of a triangle with the set of planes Z = k, where k is an integer number.
class triSegments {
public:
	std::vector<segment *> segments;

	// Calculate the segments at construction
	triSegments(Triangle *t)
	{
		Vertex *v1 = t->v1(), *v2 = t->v2(), *v3 = t->v3();
		coord minz = MIN(v1->z, MIN(v2->z, v3->z)); 
		coord Mz = MAX(v1->z, MAX(v2->z, v3->z)); 
		Vertex *minzv = (v1->z == minz) ? (v1) : ((v2->z == minz) ? (v2) : (v3));
		Vertex *Mzv = (v1->z == Mz) ? (v1) : ((v2->z == Mz) ? (v2) : (v3));
		Vertex *midzv = (v1 != minzv && v1 != Mzv) ? (v1) : ((v2 != minzv && v2 != Mzv) ? (v2) : (v3));

		bool orientation = (t->prevVertex(minzv) == Mzv);

		int minz_i = TMESH_TO_INT(ceil(minz));
		int maxz_i = TMESH_TO_INT(floor(Mz)); if (coord(maxz_i) == Mz) maxz_i--;
		int midz_i = TMESH_TO_INT(floor(midzv->z)); if (coord(midz_i) == midzv->z) midz_i--;

		coord d1, d2, d3;
		Point p1, p2;

		Edge *long_edge = minzv->getEdge(Mzv);
		Edge *short_low_edge = minzv->getEdge(midzv);
		Edge *short_high_edge = midzv->getEdge(Mzv);
		for (int j = minz_i; j <= midz_i; j++)
		{
			d1 = (coord(j) - minzv->z);
			d2 = (Mzv->z - coord(j));
			p1 = (((*minzv)*d2) + ((*Mzv)*d1)) / (d1 + d2); p1.z = coord(j);
			d3 = (midzv->z - coord(j));
			p2 = (((*minzv)*d3) + ((*midzv)*d1)) / (d1 + d3); p2.z = coord(j);
			if (orientation) segments.push_back(new segment(&p1, &p2, long_edge, short_low_edge));
			else segments.push_back(new segment(&p2, &p1, short_low_edge, long_edge));
		}

		for (int j = midz_i+1; j <= maxz_i; j++)
		{
			d1 = (coord(j) - minzv->z);
			d2 = (Mzv->z - coord(j));
			p1 = (((*minzv)*d2) + ((*Mzv)*d1)) / (d1 + d2); p1.z = coord(j);
			d3 = (coord(j) - midzv->z);
			p2 = (((*Mzv)*d3) + ((*midzv)*d2)) / (d2 + d3); p2.z = coord(j);
			if (orientation) segments.push_back(new segment(&p1, &p2, long_edge, short_high_edge));
			else segments.push_back(new segment(&p2, &p1, short_high_edge, long_edge));
		}
	}

	// This moves the segments from the internal array to a global array G
	void moveSegmentsTo(std::vector<segment *> *G)
	{
		while (!segments.empty())
		{
			G->push_back(segments.back());
			segments.pop_back();
		}
	}
};


// Reconstruct the adjacency structure of the segments starting from the connectivity of the mesh.
// This function must be called once for each mesh edge.
void reconstructAdjacenciesAcrossEdge(Edge *e)
{
	if (e->isOnBoundary()) return;
	coord minz = MIN(e->v1->z, e->v2->z);
	coord maxz = MAX(e->v1->z, e->v2->z);
	triSegments *ot1 = (triSegments *)(e->t1->info);
	triSegments *ot2 = (triSegments *)(e->t2->info);
	for (std::vector<segment *>::iterator s = ot1->segments.begin(); s != ot1->segments.end(); s++)
	if ((*s)->getHeight() >= minz && (*s)->getHeight() < maxz)
	{
		for (std::vector<segment *>::iterator c = ot2->segments.begin(); c != ot2->segments.end(); c++)
		if ((*c)->getHeight() == (*s)->getHeight()) (*s)->linkWith(*c);
	}
}


///////////////////////////////////////////////////////
//
// Structure of a CLI
//
///////////////////////////////////////////////////////

class polyline {
	std::vector<float> x, y;	// The points
	bool isHatch;

public:
	float minx, miny, maxx, maxy;	// Axis-aligned bounding box

	polyline(bool is_hatch) { minx = miny = FLT_MAX; maxx = maxy = -FLT_MAX; isHatch = is_hatch; }

	polyline(segment *s) 
	{ 
		const Point *p;
		minx = miny = FLT_MAX; maxx = maxy = -FLT_MAX; isHatch = false;
		p = s->getP1(); addPoint(TMESH_TO_FLOAT(p->x), TMESH_TO_FLOAT(p->y));
		p = s->getP2(); addPoint(TMESH_TO_FLOAT(p->x), TMESH_TO_FLOAT(p->y));

		segment *prev, *t, *r;
		prev = s;
		r = s->getNextSegment();
		while (r != NULL && r != s)
		{
			p = r->getP2();
			addPoint(TMESH_TO_FLOAT(p->x), TMESH_TO_FLOAT(p->y)); 
			t = r; r = r->oppositeNeighbor(prev); prev = t; 
		}

		removeDegeneracies();
	}

	// Returns true if the polygon is clockwise oriented (only for closed polygons)
	bool isClockwise() const
	{
		if (!isClosed()) return false;

		coord a = 0.0;
		unsigned int i, s = x.size()-1;
		for (i = 0; i < s; i++)
			a += ((x[i+1] - x[i])*(y[i+1] + y[i]));

		return (a>0.0);
	}

	void invertCurve()
	{
		unsigned int s = x.size();
		float t;
		for (unsigned int i = 0; i < s/2; i++)
		{
			t = x[i]; x[i] = x[s - i - 1]; x[s - i - 1] = t;
			t = y[i]; y[i] = y[s - i - 1]; y[s - i - 1] = t;
		}

	}


	unsigned int getNumPoints() const { return x.size(); }
	bool isEmpty() const { return (getNumPoints() == 0); }
	bool isDegenerate() const { return (getNumPoints() == 1 || getUnsignedArea()<1.0e-9); }
	bool isClosed() const { return (getNumPoints()>1 && x[0] == x.back() && y[0] == y.back()); }
	bool isOpen() const { return (getNumPoints()>1 && (x[0] != x.back() || y[0] != y.back())); }

	double getUnsignedArea() const
	{
		if (isOpen()) return DBL_MAX;
		double a = 0.0;
		unsigned int i, s = x.size()-1;
		for (i = 0; i < s; i++)
			a += ((double(x[i]) * double(y[i+1])) - (double(y[i]) * double(x[i+1])));

		return fabs(a) / 2.0;
	}

	void scaleToUnits(float s) { for (unsigned int i = 0; i < x.size(); i++) { x[i] *= s; y[i] *= s; } }

	bool containsPoint(float px, float py) const
	{
		if (!isClosed()) return false;

		unsigned int i, j, n = getNumPoints();
		bool c = false;
		for (i = 0, j = n - 1; i < n; j = i++)
		if (((y[i]>py) != (y[j]>py)) && (px < (x[j] - x[i]) * (py - y[i]) / (y[j] - y[i]) + x[i])) c = !c;
		return c;
	}

	bool isContainedIn(const polyline& p)
	{
		if (minx <= p.minx) return false;
		if (miny <= p.miny) return false;
		if (maxx >= p.maxx) return false;
		if (maxy >= p.maxy) return false;
		return (getNumPoints() && p.containsPoint(x[0], y[0]));
	}

	void addPoint(float a, float b)
	{
		x.push_back(a);
		y.push_back(b);
		if (a < minx) minx = a;
		if (a > maxx) maxx = a;
		if (b < miny) miny = b;
		if (b > maxy) maxy = b;
	}

	bool isPointRedundantOrCusp(unsigned int i) const
	{
		unsigned int ni = i+1, pi = i-1;
		if (i == 0) pi = x.size() - 2;
		else if (i == (x.size() - 1)) ni = 1;
		double ax = x[pi], ay = y[pi], bx = x[i], by = y[i], cx = x[ni], cy = y[ni];
		double acx = ax - cx, acy = ay - cy, bcx = bx - cx, bcy = by - cy;
		double cross = (acx)*(bcy) - (acy)*(bcx);
		return (fabs(cross) < 1.0e-9);
	}

	void removeRedundanciesAndCusps()
	{
		std::vector<float> nx, ny;
		unsigned int i;
		float px = FLT_MAX, py = FLT_MAX;

		if (x.size()<2) return;
		if (isClosed())
		{
			while (x.size()>2 && isPointRedundantOrCusp(x.size() - 1)) 
			{
				x.pop_back(); y.pop_back();
				x[0] = x.back(); y[0] = y.back();
			}
		}

		nx.push_back(x[0]); ny.push_back(y[0]);
		for (i = 1; i < x.size() - 1; i++)
 		 if (!isPointRedundantOrCusp(i)) { px = x[i]; py = y[i]; nx.push_back(px); ny.push_back(py); }
		nx.push_back(x[i]); ny.push_back(y[i]);

		x.clear(); y.clear();

		if (nx.size() == 2 && nx[0] == nx[1] && ny[0] == ny[1]) return;

		for (i = 0; i < nx.size(); i++) { x.push_back(nx[i]); y.push_back(ny[i]); }
	}

	void removeDegeneracies()
	{
		std::vector<float> nx, ny;
		unsigned int i;
		float px = FLT_MAX, py = FLT_MAX;

		for (i = 0; i < x.size(); i++)
		 if (x[i] != px || y[i] != py) { px = x[i]; py = y[i]; nx.push_back(px); ny.push_back(py); }
		
		x.clear(); y.clear();
		for (i = 0; i < nx.size(); i++) { x.push_back(nx[i]); y.push_back(ny[i]); }
	}

	void printWRL(FILE *wrlf, float z, bool isInnerLoop) const
	{
		fprintf(wrlf, "Separator {\nCoordinate3 {\n point [\n");
		for (unsigned int i = 0; i < getNumPoints(); i++)
			fprintf(wrlf, "%f %f %f,\n", x[i], y[i], z);
		if (isClosed() && isInnerLoop) fprintf(wrlf, "]\n}\nMaterial { diffuseColor 1 0 0 }\n");
		else if (isClosed() && !isInnerLoop) fprintf(wrlf, "]\n}\nMaterial { diffuseColor 0 0 1 }\n");
		if (isOpen()) fprintf(wrlf, "]\n}\nMaterial { diffuseColor 0 1 0 }\n");
		fprintf(wrlf, "IndexedLineSet { coordIndex [\n");
		for (unsigned int i = 0; i < getNumPoints() - 1; i++)
			fprintf(wrlf, "%d, %d, -1,\n", i, i + 1);
		fprintf(wrlf, "]\n}\n}\n");
	}

	void printCLI(FILE *clif, bool isInnerLoop) const
	{
		if (isHatch) fprintf(clif, "$$HATCH/0,");
		else fprintf(clif, "$$POLYLINE/0,");
		if (!isHatch)
		{
			if (!isClosed()) fprintf(clif, "2,");
			else if (isInnerLoop) fprintf(clif, "0,");
			else fprintf(clif, "1,");
		}
		fprintf(clif, "%d", getNumPoints());

		for (unsigned int i = 0; i < getNumPoints(); i++)
			fprintf(clif, ",%f,%f", x[i], y[i]);
		fprintf(clif, "\n");
	}

	// This may create self-intersections !
	void quantizeCoordinates(float grid_size)
	{
		for (unsigned int i = 0; i < getNumPoints(); i++) {
			x[i] /= grid_size; y[i] /= grid_size;
			x[i] = roundf(x[i]); y[i] = roundf(y[i]);
			x[i] *= grid_size; y[i] *= grid_size;
		}
	}
};

// A slice is a collection of non-intersecting polylines at a given height.
// Each polyline can be either a portion of a polygon boundary or an independent polyline.
// Boundary polylines are necessarily closed.

class slice {
	std::vector<polyline> polylines;
	std::vector<bool> innerLoopTags;
	float z;

public:
	slice(float _height) { z = _height; }

	unsigned int getNumPolylines() const { return polylines.size(); }
	unsigned int getNumPoints() const
	{
		int n = 0;
		for (unsigned int i = 0; i < polylines.size(); i++) n += polylines[i].getNumPoints();
		return n;
	}
	float getHieght() const { return z; }
	void setHeight(float f) { z = f; }

	void scaleToUnits(float s) { z *= s; for (unsigned int i = 0; i < polylines.size(); i++) polylines[i].scaleToUnits(s); }

	void addPolyline(bool inner = false, bool is_hatch = false) { polylines.push_back(polyline(is_hatch)); innerLoopTags.push_back(inner); }
	void addPolyline(segment *s, bool inner = false, bool is_hatch = false) { polylines.push_back(polyline(s)); innerLoopTags.push_back(inner); }

	bool addPointToLastPolyline(float a, float b)
	{
		polylines.back().addPoint(a, b);
		return true;
	}

	// This quantizes and simplifies each polyline. If a polyline becomes degenerate it is removed.
	// Might produce self-intersections !
	void simplifySlice(float qgrid_size)
	{
		std::vector<polyline> new_polylines = polylines;
		std::vector<bool> new_innerLoopTags = innerLoopTags;
		polylines.clear();
		innerLoopTags.clear();
		for (unsigned int i = 0; i < new_polylines.size(); i++) {
			new_polylines[i].quantizeCoordinates(qgrid_size);
			new_polylines[i].removeDegeneracies();
			new_polylines[i].removeRedundanciesAndCusps();
			if (!new_polylines[i].isEmpty() && !new_polylines[i].isDegenerate())
			{
				polylines.push_back(new_polylines[i]);
				innerLoopTags.push_back(new_innerLoopTags[i]);
			}
		}
	}

	// This method sets as 'hatch' any open polyline, and does a parity count to determine whether a polyline is
	// an inner loop. This characterization is correct only if there are no closed hatches.
	// If closed hatches are present, the problem is inherently ill-posed.

	// Closed polylines are reoriented accordingly (inner = CW, outer = CCW)
	void setTagsByGuess()
	{
		unsigned int i, j, s = polylines.size();

		if (s > 1)
		{
			for (i = 0; i < s; i++) for (j = 0; j < s; j++)
			if (i != j && polylines[i].isClosed() && polylines[i].isContainedIn(polylines[j])) innerLoopTags[i] = !innerLoopTags[i];
		}

		for (i = 0; i < s; i++)
		if ((polylines[i].isClockwise() != innerLoopTags[i])) polylines[i].invertCurve();
	}

	void printWRL(FILE *wrlf) const
	{
		for (unsigned int i = 0; i < polylines.size(); i++)
			polylines[i].printWRL(wrlf, z, innerLoopTags[i]);
	}

	void printCLI(FILE *clif) const
	{
		if (!polylines.empty()) fprintf(clif, "$$LAYER/%f\n", z);
		for (unsigned int i = 0; i < polylines.size(); i++) polylines[i].printCLI(clif, innerLoopTags[i]);
	}

	int lastPolylineNumPoints() const { return polylines.back().getNumPoints(); }

	void removeLastPolyline() { polylines.pop_back(); innerLoopTags.pop_back(); }
};

class slicing {
	std::vector<slice> slices;

	void addSlice(float _height) { slices.push_back(slice(_height)); }
	void addPolylineToLastSlice(bool inner = false, bool is_hatch = false) { slices.back().addPolyline(inner, is_hatch); }
	void addPolylineToLastSlice(segment *c, bool inner = false, bool is_hatch = false) { slices.back().addPolyline(c, inner, is_hatch); }
	void addPointToLastPolyline(float x, float y) { slices.back().addPointToLastPolyline(x, y); }
	void guessPolylineType()	{ for (unsigned int i = 0; i < slices.size(); i++) slices[i].setTagsByGuess(); }

public:
	slicing(std::vector<c_slice>& S, coord& layer_thickness)
	{
		for (std::vector<c_slice>::iterator i = S.begin(); i != S.end(); i++)
		{
			addSlice(0);
			for (std::vector<segment *>::iterator j = (*i).begin(); j != (*i).end(); j++)
			{
				addPolylineToLastSlice((*j));
				if (slices.back().lastPolylineNumPoints() < 2) slices.back().removeLastPolyline();
				else slices.back().setHeight(TMESH_TO_FLOAT((*j)->getHeight()*layer_thickness));
			}
			if (slices.back().getNumPolylines()!=0) slices.back().setTagsByGuess();
		}
	}

	unsigned int getNumSlices() const { return slices.size(); }
	unsigned int getNumPoints() const
	{
		int n = 0;
		for (unsigned int i = 0; i < slices.size(); i++) n += slices[i].getNumPoints();
		return n;
	}

	void scaleToUnits(float s) { for (unsigned int i = 0; i < slices.size(); i++) slices[i].scaleToUnits(s); }

	// This quantizes and simplifies each slice. If a slice becomes degenerate it is removed.
	// Might produce self-intersections !
	void simplifySlices(float qgrid_size)
	{
		std::vector<slice> new_slices = slices;
		slices.clear();
		for (unsigned int i = 0; i < new_slices.size(); i++) {
			new_slices[i].simplifySlice(qgrid_size);
			if (new_slices[i].getNumPolylines()!=0) slices.push_back(new_slices[i]);
			else printf("slice %d erased\n", i);
		}
	}

	bool saveWRL(const char *filename) const
	{
		FILE *wrlf = fopen(filename, "w");
		if (wrlf == NULL) return false;

		fprintf(wrlf, "#VRML V1.0 ascii\n\n");
		fprintf(wrlf, "Separator {\n");

		for (unsigned int i = 0; i < slices.size(); i++)
			slices[i].printWRL(wrlf);

		fprintf(wrlf, "}\n");
		fclose(wrlf);

		return true;
	}

	bool saveCLI(const char *filename) const
	{
		FILE *clif = fopen(filename, "w");
		if (clif == NULL) return false;

		fprintf(clif, "$$HEADERSTART\n");
		fprintf(clif, "$$ASCII\n");
		fprintf(clif, "$$UNITS/1\n");

		time_t thistime = time(0);
		tm *now = localtime(&thistime);
		fprintf(clif, "$$DATE/%.2d%.2d%.2d\n",now->tm_mday,1+now->tm_mon,now->tm_year-100);
		fprintf(clif, "$$LAYERS/%d\n", getNumSlices());
		fprintf(clif, "$$HEADEREND\n");

		fprintf(clif, "$$GEOMETRYSTART\n");
		for (unsigned int i = 0; i < slices.size(); i++) slices[i].printCLI(clif);
		fprintf(clif, "$$GEOMETRYEND\n");
		fclose(clif);

		return true;
	}
};



void usage()
{
	printf("\nSTL2CLI V0.2alpha - by Marco Attene\n------\n");
    printf("Usage: STL2CLI inmeshfile outmeshfile -l val [-q val]\n");
    printf("  Processes 'inmeshfile' and saves the result to 'outmeshfile'.\n");
	printf("  Option '-l val' specifies the layer thickness.\n");
	printf("  Option '-q val' specifies a coordinate quantization width for the slices - too large values might introduce self-intersections.\n");
	printf("  Example: STL2CLI inmeshfile -l 0.02 -q 0.01\n");
	printf("   slices with a 20 microns layer thickness while representing each slice with a 10 microns precision.\n");
	printf("  Accepted input formats are STL, PLY and OFF.\n");
	printf("\nHIT ENTER TO EXIT.\n");
	getchar();
	exit(0);
}

int main(int argc, char *argv[])
{
	ImatiSTL::init(); // This is mandatory
	ImatiSTL::app_name = "STL2CLI";
	ImatiSTL::app_version = "0.1alpha";
	ImatiSTL::app_year = "2016";
	ImatiSTL::app_authors = "Marco Attene";
	ImatiSTL::app_maillist = "attene@ge.imati.cnr.it";

	coord slicing_step = 0;
	float qgrid_size = 0.0;
    //  Matrix3x3 rot(1, 0, 0, 0, 1, 0, 0, 0, 1); // Rotation  matrix: this example does not rotate. Identity.
	//	Matrix3x3 rot(1, 0, 0, 0, 0.866, 0.5, 0, -0.5, 0.866); // Rotation  matrix: rotates -30 degrees about X axis
	//	Matrix3x3 rot(0, 0, 1, 1, 0, 0, 0, 1, 0); // Rotation  matrix: this example rotates 120 degrees about the axis x = y = z

	bool savewrl = false;

    if (argc < 3) usage();

    const char * off_filename = "tmp.off";
    const char * ann_filename = "tmp.ann";

    caxlib::unzip(argv[1], off_filename, ann_filename);

	TriMesh tin;
    if (tin.load(off_filename) != 0)
    {
        ImatiSTL::error("Can't load input file\n");
    }

    std::cout << tin.V.numels() << " vertices." << std::endl;

    caxlib::GlobalAnnotations glob_ann;
    std::vector<caxlib::VertexAnnotations> lv_ann (tin.V.numels());
    std::vector<caxlib::TriangleAnnotations> lt_ann (tin.T.numels());

    caxlib::read_ANN(ann_filename, glob_ann, lv_ann, lt_ann);

    remove (off_filename);
    remove (ann_filename);

    Matrix3x3 rot(glob_ann.orientation[0], glob_ann.orientation[1], glob_ann.orientation[2],
                    glob_ann.orientation[3], glob_ann.orientation[4], glob_ann.orientation[5],
                    glob_ann.orientation[6], glob_ann.orientation[7], glob_ann.orientation[8]); // Rotation  matrix: this example does not rotate. Identity.

	Point bb1, bb2;
	tin.getBoundingBox(bb1, bb2);
	bb2 -= bb1;
	printf("Unrotated model's bounding box size: %f x %f x %f\n", TMESH_TO_FLOAT(bb2.x), TMESH_TO_FLOAT(bb2.y), TMESH_TO_FLOAT(bb2.z));
	printf("Model's volume: %f\n", tin.volume());

	float par;
//	int i = 2;
//	if (argc > 2 && argv[2][0] == '-') i--;

//	for (; i < argc; i++)
//	{
//		if (i < argc - 1) par = (float)atof(argv[i + 1]); else par = 0;
//		if (!strcmp(argv[i], "-l"))
//		{
//			if (par <= 0) ImatiSTL::error("Layer thickness must be > 0.\n");
//			slicing_step = (coord)par;
//		}
//		else if (!strcmp(argv[i], "-q"))
//		{
//			if (par <= 0) ImatiSTL::error("Quantization width must be > 0.\n");
//			qgrid_size = par;
//		}
//		else if (!strcmp(argv[i], "-w")) savewrl = true;
//	}

    par = glob_ann.printer.layer_thickness;
    qgrid_size = par;
    slicing_step = (coord)par;

    std::cout << "Layer Thickness : " << par << std::endl;
    std::cout << "Slicing Step :" << slicing_step << std::endl;

	if (slicing_step == 0) usage();

	// Rotate the model

	Matrix4x4 transformation(
		rot.M[0], rot.M[1], rot.M[2], 0,
		rot.M[3], rot.M[4], rot.M[5], 0,
		rot.M[6], rot.M[7], rot.M[8], 0,
		0       , 0       , 0       , 1);

	tin.transform(transformation);

	// Shift it on the center of the platform
	tin.getBoundingBox(bb1, bb2);
	Point shift = (bb1 + bb2) / 2.0;
	shift.z = bb1.z;
	Node *n;
	Vertex *v;
	FOREACHVVVERTEX((&(tin.V)), v, n) (*v) -= shift;

	// Scale along the vertical direction so that slice heights become integer numbers
	FOREACHVVVERTEX((&(tin.V)), v, n) v->z /= slicing_step;

	// Calculate plane-triangle intersections at integer heights
	Triangle *t;
	FOREACHVTTRIANGLE((&(tin.T)), t, n) t->info = new triSegments(t);

	// Reconstruct the segment connectivity
	Edge *e;
	FOREACHVEEDGE((&(tin.E)), e, n) reconstructAdjacenciesAcrossEdge(e);

	std::vector<segment *> G;
	FOREACHVTTRIANGLE((&(tin.T)), t, n) ((triSegments *)t->info)->moveSegmentsTo(&G);

	// Insert in C a representative segment per connected component
	std::vector<segment *> C;
	for (std::vector<segment *>::iterator i = G.begin(); i != G.end(); i++) if (!(*i)->isMarked())
	{
		(*i)->markAndPropagate();
		C.push_back((*i));
	}

	for (unsigned int i = 0; i < C.size(); i++)
		C[i] = C[i]->getFirstSegmentInCurve(); // same as this if curve is closed
		
	std::sort(C.begin(), C.end(), segment::compSegPtr);

	// Create one c_slice at a time starting from C
	std::vector<c_slice> S;
	coord c_h, last_h = -DBL_MAX;

	for (std::vector<segment *>::iterator i = C.begin(); i != C.end(); i++)
	{
		c_h = (*i)->getHeight();
		if (c_h != last_h)
		{
			last_h = c_h;
			S.push_back(c_slice());
		}
		S.back().push_back((*i));
	}
	printf("%d slices created\n", S.size());

	// Save to CLI
	slicing sli(S, slicing_step);

	if (qgrid_size != 0.0) sli.simplifySlices(qgrid_size);
	
	printf("Total %d points constitute the slicing\n", sli.getNumPoints());
    //if (savewrl) sli.saveWRL("slicing.wrl");
    sli.saveCLI(argv[2]);

	return 0;
}

