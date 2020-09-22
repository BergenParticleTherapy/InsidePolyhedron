#include "pch.h"
#include "FindInsideOfPolyhedron.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include "DynamicArray.h"

#ifdef MATLAB_MEX_FILE
#include "mex.h"
#define printf mexPrintf
#define warning(msg) mexWarnMsgIdAndTxt("InsidePolyhedron:LogicError", msg)
#else
#define warning(msg) printf(msg)
#endif

using namespace std;

typedef vector<array<double, 3>> nBy3Array;
typedef vector<array<array<double, 3>, 3>> nBy3By3Array;

static void findExtremeCoords(const double faces[][3][3], nBy3Array &minCoords, nBy3Array &maxCoords, size_t nFaces)
{
	minCoords.resize(nFaces);
	maxCoords.resize(nFaces);
	for (int i = 0; i < nFaces; i++)
	{
		for (int dim = 0; dim < 3; dim++)
		{
			minCoords[i][dim] = faces[i][0][dim];
			if (faces[i][1][dim] < minCoords[i][dim])
				minCoords[i][dim] = faces[i][1][dim];
			if (faces[i][2][dim] < minCoords[i][dim])
				minCoords[i][dim] = faces[i][2][dim];
			maxCoords[i][dim] = faces[i][0][dim];
			if (faces[i][1][dim] > maxCoords[i][dim])
				maxCoords[i][dim] = faces[i][1][dim];
			if (faces[i][2][dim] > maxCoords[i][dim])
				maxCoords[i][dim] = faces[i][2][dim];
		}
	}
}

static void findFacesInDim(DynamicArray<int> &facesIndex, const nBy3Array &minCoords, const nBy3Array &maxCoords, double value, int dim)
{
	size_t nFaces = minCoords.size();
	facesIndex.clear();
	for (int i = 0; i < nFaces; i++)
		if (minCoords[i][dim] < value && maxCoords[i][dim] > value)
			facesIndex.insertLast_unsafe(i); //Actually safe because the array has a capacity of nFaces elements
}

template <class NBy3ArrayTemplate> /* Either an nx3x3 C-array, or a vector of 3x3 std::arrays*/

static void selectFaces(nBy3By3Array &selectedFaces, const NBy3ArrayTemplate &faces, const DynamicArray<int> &facesIndex)
{	
	size_t nSelected = facesIndex.size();
	selectedFaces.resize(nSelected);
	for (int i = 0; i < nSelected; i++)
		for (int j = 0; j < 3; j++)
			for (int k = 0; k < 3; k++)
				selectedFaces[i][j][k] = faces[facesIndex[i]][j][k];
}

static void selectCoords(nBy3Array& minCoordsDim, nBy3Array& maxCoordsDim, const nBy3Array& minCoords, 
	const nBy3Array& maxCoords, const DynamicArray<int>& facesIndex)
{
	size_t nSelected = facesIndex.size();
	minCoordsDim.clear();
	maxCoordsDim.clear();
	for (int i = 0; i < nSelected; i++)
	{
		minCoordsDim.push_back(minCoords[facesIndex[i]]);
		maxCoordsDim.push_back(maxCoords[facesIndex[i]]);
	}
}

static void solve2by2otherway(const double A[2][2], double b[2])
{

}

//Check if matrix is singular by checking if two values that are used as denominators are zero (or close to zero). 
//Warn if so, but only once.
static inline void checkSingular(double denominator1, double denominator2)
{
	static const char *singularWarning = "The plane defined by one of the triangle faces is along the line used in ray tracing. "
		"Try adding random noise to your vertex coordinates to avoid this problem.";
	static bool hasAlreadyWarned = false;

	if (abs(denominator1) < 1e-14 || abs(denominator2) < 1e-14)
		if (!hasAlreadyWarned)
		{
			warning(singularWarning);
			hasAlreadyWarned = true;
		}
}

//Solve a 2 by 2 linear equation A*x = b by Gaussian elimination with partial pivoting.
//b is overwritten by the result x.
static void solve2by2(const double A[2][2], double b[2])
{
	if (abs(A[0][0]) > abs(A[1][0]))//Partial pivoting
	{
		double fac = A[1][0] / A[0][0];
		double a22 = A[1][1] - A[0][1] * fac;		
		b[1] = (b[1] - b[0] * fac) / a22;
		b[0] = (b[0] - A[0][1] * b[1]) / A[0][0];
		checkSingular(a22, A[0][0]);
	}
	else
	{
		double fac = A[0][0] / A[1][0];
		double a12 = A[0][1] - A[1][1] * fac;
		double b1 = (b[0] - b[1] * fac) / a12;
		b[0] = (b[1] - A[1][1] * b1) / A[1][0];
		b[1] = b1;
		checkSingular(a12, A[1][0]);
	}
}

/** Get all crossings of triangular faces by a line in a specific direction */
static void getCrossings(vector<double> &crossings, const nBy3By3Array& faces, const double coords[2], const int dimOrder[3])
{
	double b[2];
	double A[2][2];
	int dim2 = dimOrder[2];
	size_t nFaces = faces.size();
	crossings.clear();

	for (int i = 0; i < nFaces; i++)
	{
		for (int dimNo = 0; dimNo < 2; dimNo++)
		{
			int dim = dimOrder[dimNo];
			b[dimNo] = coords[dimNo] - faces[i][0][dim];
			A[dimNo][0] = faces[i][1][dim] - faces[i][0][dim];
			A[dimNo][1] = faces[i][2][dim] - faces[i][0][dim];
		}
		solve2by2(A, b);
		if (b[0] > 0 && b[1] > 0 && ((b[0] + b[1]) < 1))
		{
			//No pun intended
			double crossing = faces[i][0][dim2] + b[0] * (faces[i][1][dim2] - faces[i][0][dim2]) + b[1] * (faces[i][2][dim2] - faces[i][0][dim2]);
			crossings.push_back(crossing);
		}
	}
	sort(crossings.begin(), crossings.end());
}


static inline bool isOdd(int n)
{
	return (n % 2) == 1;
}

static void warnOnce(const char *msg)
{
	static bool hasWarned = false;
	if (!hasWarned)
	{
		warning(msg);
		hasWarned = true;
	}
}

static void buildFaceMatrix(double faces[][3][3], const double vertices[][3], const int faceIndices[][3], size_t nFaces)
{
	for (int i = 0; i < nFaces; i++)
		for (int j = 0; j < 3; j++)
			for (int k = 0; k < 3; k++)
				faces[i][j][k] = vertices[faceIndices[i][j]][k];
}


//Selects the order in which the dimensions are processed
static void selectDimensionsForFastestProcessing(int dimOrder[3], size_t dimSize[3], size_t dimStep[3])
{
	size_t nx = dimSize[0];
	size_t ny = dimSize[1];

	//Bubble sort
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 2 - i; j++)
		{
			if (dimSize[j] > dimSize[j + 1])
			{
				size_t temp = dimSize[j];
				dimSize[j] = dimSize[j + 1];
				dimSize[j + 1] = temp;
				int tempi = dimOrder[j];
				dimOrder[j] = dimOrder[j + 1];
				dimOrder[j + 1] = tempi;
			}
		}
	}

	dimStep[dimOrder[0]] = ny;
	dimStep[dimOrder[1]] = 1;
	dimStep[dimOrder[2]] = ny * nx;
}


/**
\brief Check whether a set of points on a 3D-grid is inside or outside a surface defined by a polyhedron.
This function uses ray-tracing to determine whether or not a point is inside the surface. Since the points
to be checked are aligned on a grid, we can reuse information for each point to perform the calculation 
significantly faster than if we were to check each point individually.
\param inside[out] Boolean array of output values, must be large enough to contain nx*ny*nz values. The result corresponding to the coordinate (x[i], y[j], z[k]) is found
in inside[(j * nx * ny) + (i * ny) +  k]. The reason for this configuration is to align with Matlab's meshgrid(x, y, z) function.
\param vertices[in] Array of vertices in the polyhedron. Each vertex consists of 3 coordinates, x, y and z, therefore this is an n x 3 array.
\param faceIndices[in] Definition of the triangular faces of the surface. Each row of this matrix consists of three indices into the vertex-list, which together define
a triangular face.
\param nFaces Number of faces in the surface (size of faceIndices)
\param x X-coordinate values on the grid to be checked. 
\param nx Number of X-coordinates
\param y Y-coordinate values on the grid to be checked.
\param ny Number of Y-coordinates
\param z Z-coordinate values on the grid to be checked.
\param nz Number of Z-coordinates
*/
void insidePolyhedron(bool inside[], const double vertices[][3], const int faceIndices[][3], size_t nFaces, const double x[], size_t nx, const double y[], size_t ny, const double z[], size_t nz)
{
	double (*faces)[3][3] = new double[nFaces][3][3];
	buildFaceMatrix(faces, vertices, faceIndices, nFaces);
	insidePolyhedron(inside, faces, nFaces, x, nx, y, ny, z, nz);
	delete[] faces;
}

/**
\brief Check whether a set of points on a 3D-grid is inside or outside a surface defined by a polyhedron.
This function uses ray-tracing to determine whether or not a point is inside the surface. Since the points
to be checked are aligned on a grid, we can reuse information for each point to perform the calculation
significantly faster than if we were to check each point individually.
This function is exactly the same as the other insidePolyhedron function, except that the surface is defined in a single list of triangular faces instead of a separate
list of vertices and faces.
\param inside[out] Boolean array of output values, must be large enough to contain nx*ny*nz values. The result corresponding to the coordinate (x[i], y[j], z[k]) is found
in inside[(j * nx * ny) + (i * ny) +  k]. The reason for this configuration is to align with Matlab's meshgrid(x, y, z) function.
\param vertices[in] Array of vertices in the polyhedron. Each vertex consists of 3 coordinates, x, y and z, therefore this is an n x 3 array.
\param faces[in] Definition of the triangular faces of the surface. faces[i][j][k] represents the k-coordinate (where x=0, y=1, z= 2) of the j'th vertex of the i'th face 
of the polyhedron.
\param nFaces Number of faces in the surface (size of faces)
\param x X-coordinate values on the grid to be checked.
\param nx Number of X-coordinates
\param y Y-coordinate values on the grid to be checked.
\param ny Number of Y-coordinates
\param z Z-coordinate values on the grid to be checked.
\param nz Number of Z-coordinates
*/
void insidePolyhedron(bool inside[], const double faces[][3][3], size_t nFaces, const double x[], size_t nx, const double y[], size_t ny, const double z[], size_t nz)
{
	nBy3Array minCoords;
	nBy3Array maxCoords;	
	vector<double> crossings;
	nBy3Array minCoordsD2;
	nBy3Array maxCoordsD2;
	nBy3By3Array facesD2;
	nBy3By3Array facesD1;
	size_t dimSteps[3];

	DynamicArray<int> facesIndex {nFaces};

	size_t dimSize[3] = {nx, ny, nz};
	int dimOrder[3] = {0, 1, 2};
	
	selectDimensionsForFastestProcessing(dimOrder, dimSize, dimSteps);
	const int dim0 = dimOrder[0], dim1 = dimOrder[1], dim2 = dimOrder[2];

	findExtremeCoords(faces, minCoords, maxCoords, nFaces);

	const double *gridCoords[] = {x, y, z};

	for (int i = 0; i < dimSize[0]; i++)
	{
		findFacesInDim(facesIndex, minCoords, maxCoords, gridCoords[dim0][i], dim0);
		if (facesIndex.size() == 0)
			continue;
		selectFaces(facesD2, faces, facesIndex);
		selectCoords(minCoordsD2, maxCoordsD2, minCoords, maxCoords, facesIndex);
		for (int j = 0; j < dimSize[1]; j++)
		{
			const double coords[2] = {gridCoords[dim0][i], gridCoords[dim1][j]};
			findFacesInDim(facesIndex, minCoordsD2, maxCoordsD2, coords[1], dim1);
			if (facesIndex.size() == 0)
				continue;
			selectFaces(facesD1, facesD2, facesIndex);
			getCrossings(crossings, facesD1, coords, dimOrder);
			size_t nCrossings = crossings.size();
			if (nCrossings == 0)
				continue;
			if (isOdd(nCrossings))
				warnOnce("Odd number of crossings found. The polyhedron may not be closed, or one of the triangular faces may lie in the exact direction of the traced ray.");
			bool isInside = false;
			int crossingsPassed = 0;
			for (int k = 0; k < dimSize[2]; k++)
			{
				while ((crossingsPassed < nCrossings) && (crossings[crossingsPassed] < gridCoords[dim2][k]))
				{
					crossingsPassed++;
					isInside = !isInside;
				}
				inside[i * dimSteps[0] + j * dimSteps[1] + k * dimSteps[2]] = isInside;
			}
		}
	}
}


