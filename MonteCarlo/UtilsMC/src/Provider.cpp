#include "Provider.h"
#include <stdexcept>
#include <algorithm>
#include "iostream"

unsigned int Provider::nInstances = 0;
std::unique_ptr<TRandom> Provider::randGen = std::make_unique<TRandom3>(0);

void Provider::SetSingleParam(const std::string &pname, const double &pval) {
    ValidateParamName(pname);
    paramVals[pname] = pval;
    ValidateParamValues();
}

double Provider::GetParam(const std::string &pname) {
    ValidateParamName(pname);
    return paramVals[pname];
}

void Provider::ValidateParamName(const std::string &pname) {
    if (paramVals.find(pname) == paramVals.end())
        throw std::runtime_error(GetName()+"::ValidateParamName: Unknown parameter \'" + pname + "\'!");
}

void Provider::SetParams(const paramMapType &pars) {
    for (auto &p: pars)
    {
        ValidateParamName(p.first);
        paramVals[p.first] = p.second;
    }
    ValidateParamValues();
}

std::vector<std::string> Provider::GetParamNames() {
    std::vector<std::string> v;
    std::transform(paramVals.begin(), paramVals.end(),
                   std::back_inserter(v),
                   [](auto &p) { return p.first; });
    return v;
}

void Provider::PrintParams() {
    std::cout << "Parameters of " << GetName() << ":\n";
    std::for_each(paramVals.begin(), paramVals.end(),
                  [](auto &p) {
                      std::cout << '\t' << p.first << ": " << p.second << "\n";
                  }
    );
    std::cout << std::flush;
}

void Provider::CheckCondition(bool cond, const std::string& message)
{
    if(!cond){
        auto msg=GetName()+"::ValidateParamValues: ";
        msg+=message;
        throw std::invalid_argument(msg);
    }
}
