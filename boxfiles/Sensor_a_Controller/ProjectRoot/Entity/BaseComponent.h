#ifndef BASE_COMPONENT_H
#define BASE_COMPONENT_H

#include <string>
#include <iostream>

namespace BoxSystem {

class BaseComponent {
protected:
    std::string compID;       
    std::string parentNodeID; 
    std::string name;         

public:
    BaseComponent(const std::string& id, const std::string& nodeID)
        : compID(id), parentNodeID(nodeID), name("") {}

    virtual ~BaseComponent() = default;
    
    void setName(const std::string& n) { name = n; }
    std::string getIdentity() const { return parentNodeID + "::" + compID; }
    virtual void execute() = 0;
};

} 
#endif