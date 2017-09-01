// createEDP.cpp
// purpose - given a building, return an EVENT for the Haywired data.
// written: fmckenna

#include <iostream>

#include <stdio.h>
#include <stdlib.h>

#include "csvparser.h"   // for parsing csv file
#include <jansson.h>     // for writing json
#include <nanoflann.hpp> // for searching for nearest point

#include <map>
#include <string>

using namespace nanoflann;

struct locations {
  locations():x(0),y(0) {}
  locations(std::string st,double a,double b):station(st),x(a),y(b) {}
  std::string station;
  double x;
  double y;
};


template <typename T>
struct PointCloud
{
  struct Point
  {
    Point(): stationTag(0),x(0.),y(0.) {}
    Point(int tag, T(a), T(b)): stationTag(tag),x(a),y(b) {}
    int stationTag;
    T  x,y;
  };
  
  std::vector<Point>  pts;
  
  inline size_t kdtree_get_point_count() const { return pts.size(); }
  
  inline T kdtree_distance(const T *p1, const size_t idx_p2,size_t /*size*/) const
  {
    const T d0=p1[0]-pts[idx_p2].x;
    const T d1=p1[1]-pts[idx_p2].y;
    return d0*d0+d1*d1;
  }
  
  inline T kdtree_get_pt(const size_t idx, int dim) const
  {
    if (dim==0) return pts[idx].x;
    else return pts[idx].y;
  }
  
  template <class BBOX>
  bool kdtree_get_bbox(BBOX& /* bb */) const { return false; }
};

int main(int argc, char **argv) {

  if (argc != 3) {
    printf("ERROR correct usage: createEvent fileNameBIM fileNameEVENT\n");
    exit(0);
  }

  char *filenameBIM = argv[1];
  char *filenameEVENT = argv[2];

  std::map<int, locations> stationLocations;
  PointCloud<float> cloud;

  //
  // first parse the station file & put each station into the cloud of points
  //
  
  CsvParser *csvparser = CsvParser_new("HFmeta", " ", 1);
  const CsvRow *header = CsvParser_getHeader(csvparser);
  
  if (header == NULL) {
        printf("%s\n", CsvParser_getErrorMessage(csvparser));
        return 1;
  }
  
  const char **headerFields = CsvParser_getFields(header);
  for (int i = 0 ; i < CsvParser_getNumFields(header) ; i++) {
    printf("TITLE: %d %s\n", i, headerFields[i]);
  }    
  CsvRow *row;
  
  int count = 0;
  while ((row = CsvParser_getRow(csvparser))) {
    const char **rowFields = CsvParser_getFields(row);
    //           for (int i = 0 ; i < CsvParser_getNumFields(row) ; i++) {
    //             printf("FIELD: %s\n", rowFields[i]);
    //           }
    char *pEnd;
    std::string station(rowFields[0]);
    double x = strtod(rowFields[4],&pEnd);
    double y = strtod(rowFields[5],&pEnd);
    stationLocations[count]=locations(station,x,y);
    cloud.pts.resize(count+1);
    cloud.pts[count].stationTag = count;
    cloud.pts[count].x = x;
    cloud.pts[count].y = y;
    count++;
  }
  
  CsvParser_destroy(csvparser);

  //
  // now parse the bim file for the location and 
  //

  json_error_t error;
  json_t *root = json_load_file(filenameBIM, 0, &error);

  if(!root) {
    printf("ERROR reading BIM file: %s\n", filenameBIM);
  }

  json_t *GI = json_object_get(root,"GI");
  json_t *location = json_object_get(GI,"location");

  float buildingLoc[2];
  buildingLoc[0] = json_number_value(json_object_get(location,"latitude"));
  buildingLoc[1] = json_real_value(json_object_get(location,"longitude"));

  json_object_clear(root);  

  //
  // now find nearest point in the cloud
  //

  // build the kd tree
  typedef KDTreeSingleIndexAdaptor<L2_Simple_Adaptor<float, PointCloud<float> >,
    PointCloud<float>,
    2
    > my_kd_tree_t;

  my_kd_tree_t   index(2, cloud, KDTreeSingleIndexAdaptorParams(10) );
  index.buildIndex();

  //
  // do a knn search to find nearest point
  //

  long unsigned int num_results = 1;
  long unsigned int ret_index;
  float out_dist_sqr;
  nanoflann::KNNResultSet<float> resultSet(num_results);
  resultSet.init(&ret_index, &out_dist_sqr);
  index.findNeighbors(resultSet, &buildingLoc[0], nanoflann::SearchParams(10));

  // 
  // create the event
  //

  int stationTag = ret_index;

  std::map<int, locations>::iterator stationIter;

  stationIter = stationLocations.find(stationTag);
  std::string stationName;

  if (stationIter != stationLocations.end()) {
    //std::cerr << stationIter->second.station;
    stationName = stationIter->second.station + ".json";
    std::cerr << stationName;
  }

  //
  // add acceleration record at station to event array in events file
  //

  error;
  root = json_load_file(filenameEVENT, 0, &error);
  json_t *eventsArray;

  if(!root) {
    root = json_object();    
    eventsArray = json_array();    
    json_object_set(root,"Events",eventsArray);
  } else {
    eventsArray = json_object_get(root,"Events");
  }

  json_t *rootNewEvent = json_load_file(stationName.c_str(), 0, &error);
  if(!rootNewEvent) {
    std::cerr << "THAT FAILED\n" << stationName;
  } else {
    json_array_append(eventsArray,rootNewEvent);
  }

  json_dump_file(root,filenameEVENT,0);
  json_object_clear(root);

  return 0;
}