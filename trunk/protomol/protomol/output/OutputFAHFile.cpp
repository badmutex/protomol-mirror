#include <protomol/output/OutputFAHFile.h>
#include <protomol/ProtoMolApp.h>
#include <protomol/module/MainModule.h>
#include <protomol/module/OutputModule.h>
#include <protomol/type/String.h>

using namespace std;
using namespace ProtoMol::Report;
using namespace ProtoMol;

//____ OutputFAHFile
const string OutputFAHFile::keyword("FAHFile");

OutputFAHFile::OutputFAHFile() {}

OutputFAHFile::OutputFAHFile(const string &filename, int freq) :
  Output(freq), filename(filename) {}

void OutputFAHFile::doInitialize() {}

void OutputFAHFile::doRun(int step) {
  open(filename.c_str(), ios::out | ios::trunc);

  if (is_open()) {
    string str;
    str = String(app->positions.size()) + '\t' + "Alanine" + "\n";
    file.write(str.c_str(), str.length());

    for (unsigned int i = 0; i < app->positions.size(); i++) {
      str = String(i + 1) + '\t' +
        app->topology->atomTypes[app->topology->atoms[i].type].name + '\t' +
        String(app->positions[i].x) + '\t' +
        String(app->positions[i].y) + '\t' +
        String(app->positions[i].z) + '\t' +
        String(1) + "\n";

      file.write(str.c_str(), str.length());
    }
    
    close();
  }
}

void OutputFAHFile::doFinalize(int step) {}

Output *OutputFAHFile::doMake(const vector<Value> &values) const {
  return new OutputFAHFile(values[0], values[1]);
}

bool OutputFAHFile::isIdDefined(const Configuration *config) const {
  return config->valid(getId());
}

void OutputFAHFile::getParameters(vector<Parameter> &parameter) const {
  parameter.push_back
    (Parameter(getId(), Value(filename, ConstraintValueType::NotEmpty())));
  parameter.push_back
    (Parameter(keyword + "OutputFreq",
               Value(myOutputFreq, ConstraintValueType::Positive())));
}

bool OutputFAHFile::adjustWithDefaultParameters(vector<Value> &values,
                                               const Configuration *config)
const {
  if (!checkParameterTypes(values)) return false;

  if (config->valid(InputOutputfreq::keyword) && !values[1].valid())
    values[1] = (*config)[InputOutputfreq::keyword];

  return checkParameters(values);
}