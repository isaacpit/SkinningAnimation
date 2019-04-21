#include <iostream>
#include <fstream>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
// #include <glm/gtc/type_ptr.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "ShapeSkin.h"
#include "GLSL.h"
#include "Program.h"

using namespace std;

void printMat4(glm::mat4 m) 
{
	for(int i = 0; i < 4; ++i) {
		for(int j = 0; j < 4; ++j) {
			// mat[j] returns the jth column
			printf("%- 5.2f ", m[j][i]);
		}
		printf("\n");
	}
	printf("\n");
}

void printVec4(glm::vec4 v) {
	for (int i = 0; i < 4; ++i) {
		printf("%- 5.2f ", v[i]);
	}
	printf("\n");
	// cout << "v ( " << v.x << ", " << v.y << ", " << v.z << endl;
	
}

ShapeSkin::ShapeSkin() :
	prog(NULL),
	elemBufID(0),
	posBufID(0),
	norBufID(0),
	texBufID(0)
{
}

ShapeSkin::~ShapeSkin()
{
}

void ShapeSkin::loadMesh(const string &meshName)
{
	// Load geometry
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	string errStr;
	bool rc = tinyobj::LoadObj(&attrib, &shapes, &materials, &errStr, meshName.c_str());
	if(!rc) {
		cerr << errStr << endl;
	} else {
		posBuf = attrib.vertices;
		norBuf = attrib.normals;
		texBuf = attrib.texcoords;
		assert(posBuf.size() == norBuf.size());
		// Loop over shapes
		for(size_t s = 0; s < shapes.size(); s++) {
			// Loop over faces (polygons)
			const tinyobj::mesh_t &mesh = shapes[s].mesh;
			size_t index_offset = 0;
			for(size_t f = 0; f < mesh.num_face_vertices.size(); f++) {
				size_t fv = mesh.num_face_vertices[f];
				// Loop over vertices in the face.
				for(size_t v = 0; v < fv; v++) {
					// access to vertex
					tinyobj::index_t idx = mesh.indices[index_offset + v];
					elemBuf.push_back(idx.vertex_index);
				}
				index_offset += fv;
				// per-face material (IGNORE)
				shapes[s].mesh.material_ids[f];
			}
		}
	}
}

void ShapeSkin::loadAttachment(const std::string &filename)
{
	int nverts, nbones;
	ifstream in;
	in.open(filename);
	if(!in.good()) {
		cout << "Cannot read " << filename << endl;
		return;
	}
	string line;
	getline(in, line); // comment
	getline(in, line); // comment
	getline(in, line);
	stringstream ss0(line);
	ss0 >> nverts;
	ss0 >> nbones;
	assert(nverts == posBuf.size()/3);
	weightBuf = std::vector<std::vector<float > > (nverts);
	int idx = 0;
	while(1) {
		getline(in, line);
		if(in.eof()) {
			break;
		}
		
		stringstream ss(line);
		weightBuf.at(idx) = std::vector<float>(nbones);
		for (int i = 0; i < nbones; ++i) {
			ss >> weightBuf.at(idx).at(i);
		}
		idx++;
	}

	cout << "weightBuf.sz: " << weightBuf.size() << endl;
	// for (int i = 0; i < nverts; ++i) {
	// 	for (int j = 0; j < nbones; ++j) {
	// 		cout << weightBuf.at(i).at(j) << " ";
	// 	} cout << endl;
	// }
	in.close();
}

void ShapeSkin::loadSkeleton(const std::string &filename) {
	ifstream in;
	string line;
	in.open(filename);
	if(!in.good()) {
		cout << "Cannot read " << filename << endl;
		return;
	}
	getline(in, line); // comment 
	getline(in, line); // comment 
	getline(in, line); // comment 
	getline(in, line);
	stringstream ss0(line);
	ss0 >> nverts;
	ss0 >> nbones;
	printf("loading skeleton file: %s \n", filename.c_str());
	printf("(verts, bones): (%d , %d)\n", nverts, nbones);
	
	int nBones = 18;
	vecTransforms = std::vector<std::vector<glm::mat4>>(nverts+2);
	bindPoseNoInverse = vector<glm::mat4>(nBones);
	bindPoseInverse = vector<glm::mat4>(nBones);
	int nLine = 0;
	while (!in.eof()) {
		getline(in, line);
		stringstream ss(line);
		// cout << "i: " << nLine << endl;
		// printf("%s\n", string(line).c_str());
		float qx, qy, qz, qw, px, py, pz;
		// cerr << "nLine: " << nLine << " | " << endl;
		for (int i = 0; i < nBones; ++i) {
			ss >> qx;
			ss >> qy;
			ss >> qz;
			ss >> qw;
			ss >> px;
			ss >> py;
			ss >> pz;
			glm::quat q(qw, qx, qy, qz);
			glm::vec4 p(px, py, pz, 1.0f);
			glm::mat4 E = mat4_cast(q);
			E[3] = p;
			// cerr << i << " " << endl;
			if (nLine == 0) { // bind pose
				bindPoseInverse.at(i) = glm::inverse(E); 
				bindPoseNoInverse.at(i) = E;
				// cout << "i: " << i << endl;
				// printMat4(E * bindPose.at(i));
			}
			else {
				// first bone
				if (i == 0) vecTransforms.at(nLine-1) = std::vector<glm::mat4>(nBones);
				vecTransforms.at(nLine-1).at(i) = E;
			}

			if (((nLine == 1) || (nLine == 0)) && i == 0) {
				printMat4(E);
			}
		}
		// cerr << endl;
		nLine++;
	}

	in.close();
	
}

void ShapeSkin::init()
{
	// Send the position array to the GPU
	glGenBuffers(1, &posBufID);
	glBindBuffer(GL_ARRAY_BUFFER, posBufID);
	glBufferData(GL_ARRAY_BUFFER, posBuf.size()*sizeof(float), &posBuf[0], GL_STATIC_DRAW);
	
	// Send the normal array to the GPU
	glGenBuffers(1, &norBufID);
	glBindBuffer(GL_ARRAY_BUFFER, norBufID);
	glBufferData(GL_ARRAY_BUFFER, norBuf.size()*sizeof(float), &norBuf[0], GL_STATIC_DRAW);

	// No texture info
	texBufID = 0;
	
	// Send the element array to the GPU
	glGenBuffers(1, &elemBufID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elemBufID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, elemBuf.size()*sizeof(unsigned int), &elemBuf[0], GL_STATIC_DRAW);
	
	// Unbind the arrays
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	assert(glGetError() == GL_NO_ERROR);
}

void ShapeSkin::drawBindPoseFrenetFrames(std::shared_ptr<MatrixStack> MV, float t, bool debug, float len) {
	
	for (int i = 0; i < nbones; ++i) {
		drawPoint(MV, bindPoseNoInverse.at(i), t, debug, len);
	}
	
	glLineWidth(1);

}

void ShapeSkin::drawAnimationFrenetFrames(std::shared_ptr<MatrixStack> MV, float t, bool debug, float len) {
	// cerr << "t: " << t << endl;
	float speed = 10.0f;
	int idx = fmod(t, nbones) * speed;
	// cerr << idx << endl;
	for (int i = 0; i < nbones; ++i) {
		// cout << "i: " << i << endl;
		drawPoint(MV, vecTransforms.at(idx).at(i), t, debug, len);
	}
	
	glLineWidth(1);

}

void ShapeSkin::drawPoint(std::shared_ptr<MatrixStack> MV, glm::mat4 E, float t, bool debug, float len) {
	MV->pushMatrix();
	glLineWidth(3);
	glBegin(GL_LINES);			
			
		MV->multMatrix(E);
		glm::vec4 p = E[3] ;
		glColor3f(1.0f, 0, 0);
		glVertex3f(p.x, p.y , p.z);
		glVertex3f(p.x+len, p.y, p.z);
		glColor3f(0, 1.0f, 0);
		glVertex3f(p.x, p.y , p.z);
		glVertex3f(p.x, p.y+len, p.z);
		glColor3f(0, 0, 1.0f);
		glVertex3f(p.x, p.y , p.z);
		glVertex3f(p.x, p.y, p.z+len);

		if (debug) {
			// cout << "i: " << i << endl;
			printMat4(E);
			printVec4(E[0]);
			printVec4(E[1]);
			printVec4(E[2]);
			printVec4(E[3]);
			cout << "p" << endl;
			printVec4(p);
		}

	glEnd();
	MV->popMatrix();
}

void ShapeSkin::draw() const
{
	assert(prog);
	
	int h_pos = prog->getAttribute("aPos");
	glEnableVertexAttribArray(h_pos);
	glBindBuffer(GL_ARRAY_BUFFER, posBufID);
	glVertexAttribPointer(h_pos, 3, GL_FLOAT, GL_FALSE, 0, (const void *)0);
	
	int h_nor = prog->getAttribute("aNor");
	glEnableVertexAttribArray(h_nor);
	glBindBuffer(GL_ARRAY_BUFFER, norBufID);
	glVertexAttribPointer(h_nor, 3, GL_FLOAT, GL_FALSE, 0, (const void *)0);
	
	// Draw
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elemBufID);
	glDrawElements(GL_TRIANGLES, (int)elemBuf.size(), GL_UNSIGNED_INT, (const void *)0);
	
	glDisableVertexAttribArray(h_nor);
	glDisableVertexAttribArray(h_pos);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}


