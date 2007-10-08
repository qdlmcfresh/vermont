#include "ObserverCfg.h"
#include "common/msg.h"
#include "common/PacketInstanceManager.h"
#include "cfg/XMLElement.h"

#include "sampler/Observer.h"

#include <string>
#include <vector>
#include <cassert>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

ObserverCfg* ObserverCfg::create(XMLElement* e)
{
	assert(e);
	assert(e->getName() == getName());
	return new ObserverCfg(e);
}

ObserverCfg::ObserverCfg(XMLElement* elem)
	: Cfg(elem), CfgHelper<Observer>(), interface(), pcap_filter(), capture_len(0)
{

}

ObserverCfg::~ObserverCfg()
{
}

Observer* ObserverCfg::getInstance()
{
	if (instance)
		return instance;


	XMLNode::XMLSet<XMLElement*> set = _elem->getElementChildren();
	for (XMLNode::XMLSet<XMLElement*>::iterator it = set.begin();
	     it != set.end();
	     it++) {
		XMLElement* e = *it;

		if (e->matches("interface")) {
			interface = e->getFirstText();
		} else if (e->matches("capture_len")) {
			capture_len = atoi(e->getFirstText().c_str());
		} else if (e->matches("pcap_filter")) {
			pcap_filter = e->getFirstText();
		} else if (e->matches("timeBased")) {

		} else {
			msg(MSG_FATAL, "Unkown observer config statement %s\n", e->getName().c_str());
			continue;
		}
	}

	instance = new Observer(interface, PacketInstanceManager::getManager());

	if (capture_len) {
		if(!instance->setCaptureLen(capture_len)) {
			msg(MSG_FATAL, "Observer: wrong snaplen specified - using %d",
					instance->getCaptureLen());
		}
	}

	if (!pcap_filter.empty()) {
		if (!instance->prepare(pcap_filter.c_str())) {
			msg(MSG_FATAL, "Observer: preparing failed");
			THROWEXCEPTION("Observer setup failed!");
		}
	}

	return instance;
}


void ObserverCfg::connectInstances(Cfg* other)
{
	instance = getInstance();

	int need_adapter = 0;
	need_adapter |= ((getNext().size() > 1) ? NEED_SPLITTER : NO_ADAPTER);

	if ((dynamic_cast<Notifiable*>(other->getInstance()) != NULL) &&
	    (dynamic_cast<Timer*>(instance) == NULL))
		need_adapter |= NEED_TIMEOUT;
	
	connectTo(other->getInstance(), need_adapter);
}

bool ObserverCfg::deriveFrom(ObserverCfg* old)
{
	if (interface != old->interface)
		return false;
	if (capture_len != old->capture_len)
		return false;
	if (pcap_filter != old->pcap_filter)
		return false;

	transferInstance(old);
	return true;
}