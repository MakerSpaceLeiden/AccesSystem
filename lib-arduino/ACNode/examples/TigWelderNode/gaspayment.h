// NOTE: not a real .h file - but Arduino style `ino' hack.
//
#ifndef gaspayment_h
#define gaspayment_h

PaymentAPI * paymentAPI = NULL;

void settle_claim() {
    if (!paymentAPI || !paymentAPI->ready())
        return;

    if (!wr.last_claim[0])
        return;
            
    unsigned long seconds_used = wr.welding_timer - wr.welding_timer_start_last_session;
    float minutes =  seconds_used / 60.0;
    float amount = minutes * wr.price_per_minute;
    
    char buff[128];
    snprintf(buff,sizeof(buff),"TIG welding gas, %.1f minutes @ %.2f (on reset)",
        minutes, amount);
        
    bool res = paymentAPI->settle(wr.last_claim,amount,buff, "Settled on boot of node (after cresh/reset/powercycle node)");
                
    // We're not going to make a cow on things failing - as this is still experimental.
    Log.printf("Payment %.2f %s\n", amount, res ? "OK" : "FAILED");

    if (!res)
        return;
    
    bzero(wr.last_claim, sizeof(wr.last_claim));
    welding_save(true);
}

void claim() {
    if (!paymentAPI || !paymentAPI->ready())
        return;

    // best effort attempt to settle; otherwise we just chalk this up
    // as a lost and let the claim que & a human deal with this on the
    // CRM side.
    if (wr.last_claim[0])
        settle_claim();
        
    ApprovalEntry * e = node.lastApproved();
    if (!e) {
        Log.println("Could not create a payment claim - no known user.");
        return;
    };
    
    wr.welding_timer_start_last_session = wr.welding_timer;
    wr.amount_claimed = wr.price_per_minute;
    
    char * claim = paymentAPI->claim(e->uid.c_str(),wr.amount_claimed,"Up to 1 minute of TIG gas");
    if (!claim) {
        // do we pospone this ? or for now accept our 'risk' ?
        Log.println("Could not create a payment claim - registration failed.");
        return;
    };
    strncpy(wr.last_claim, claim, sizeof(wr.last_claim,claim));
    welding_save(true);

    Log.printf("Claim %s of %.2f registerd for %s (%s)", claim, wr.amount_claimed, e->name.c_str(), e->uid.c_str());
    free(claim);
}

void update_claim() {
    if (!paymentAPI || !paymentAPI->ready())
        return;

    unsigned long seconds_used = wr.welding_timer - wr.welding_timer_start_last_session;
    
    if (seconds_used < 10)
        return;
    
    float minutes =  seconds_used / 60.0;
    float amount = minutes * wr.price_per_minute;

    if (amount < 0.80 * wr.amount_claimed)
        return;

    if (amount < 10)
        amount *= 1.25;
    else
        amount += 2;
    
    // Round up to the next nice minute
    amount =  (1.+(int)(amount / amount/wr.price_per_minute)) * wr.price_per_minute;
    
    char buff[128];
    snprintf(buff,sizeof(buff),"TIG welding gas, up to %.1f minutes @ %.2f (on reset)",
                amount/wr.price_per_minute, amount);

    if (!wr.last_claim[0]) {
        Log.printf("Could not update a payment claim for %s, no claim pending.",buff);
        return;
    };

    if (!paymentAPI->update(wr.last_claim, amount, buff, "Mid welding update")) {
        Log.println("Could not update a payment claim.");
        return;
    };
    
    wr.amount_claimed = amount;
    welding_save(true);

    Log.printf("Claim %s updated to %.2f", wr.last_claim, amount);
}

void payment_init() {
    paymentAPI = new PaymentAPI(node._restAPI);
    paymentAPI->onReady([&]() -> void {
        if (!paymentAPI->fetchPricelist() ||
            !paymentAPI->pricelist ||
            !paymentAPI->pricelist->defaultItem
        ) {
            if (wr.price_per_minute == 0) {
                Log.println("No valid prices (and none cached either)");
                node.updateDisplayStateMsg("no price list", 2);
                node.machinestate = MachineState::OUTOFORDER;
                return;
            };
            Log.printf("Pricing falls back to cached value: %.02f\n",
                wr.price_per_minute);
        } else {
            Log.printf("Pricing: %s (%s): %.02f\n",
                paymentAPI->pricelist->defaultItem->name,
                paymentAPI->pricelist->defaultItem->desc,
                paymentAPI->pricelist->defaultItem->price);

                if (wr.price_per_minute != paymentAPI->pricelist->defaultItem->price) {
                    wr.price_per_minute = paymentAPI->pricelist->defaultItem->price;
                    Log.printf("Writing updated price to NVRAM\n");
                    welding_save();
                };
        };

        // Check if there are pending claims & try to settle these. We even
        // do this when no gas was used - so we close out any past use cleanly.
        if (wr.last_claim[0]) {
                settle_claim();
        };
    });
}

void payment_loop() {
    if (wr.last_claim[0])
        return;

    static unsigned long lst = millis();

    if (millis() - lst < 100 * 1000)
        return;
    lst = millis();

    if (!paymentAPI || !paymentAPI->ready())
        return;

    if (node.machinestate == MachineState::WAITINGFORCARD)
        settle_claim();
    
    if (node.machinestate == POWERED)
        update_claim();
}

#endif /* gaspayment_h */
