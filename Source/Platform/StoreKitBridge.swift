import Foundation
import StoreKit
import UIKit

// StoreKit 2, and nothing else: no server, no receipt parsing. Every answer is
// read from transactions Apple signed (`.verified`), so a patched response or a
// tampered local file cannot grant access. The trial is the subscription's
// introductory offer, which Apple tracks per Apple ID.
//
// Reached from C++ through Subscription.mm. Callbacks arrive on the main
// thread, which on iOS is the JUCE message thread.

@objc(VPPlan) public final class VPPlan: NSObject {
    @objc public var id = ""
    @objc public var price = ""
    @objc public var pricePerMonth = ""
    @objc public var yearly = false
    @objc public var savePercent = 0
    @objc public var trialEligible = false
    @objc public var trialDays = 0
}

@objc(VPStatus) public final class VPStatus: NSObject {
    /// 0 checking, 1 locked, 2 trial, 3 subscribed
    @objc public var access = 0
    @objc public var productsLoaded = false
    @objc public var productsFailed = false
    @objc public var plans: [VPPlan] = []
    @objc public var activePlanId = ""
    @objc public var expiresAt = 0.0
    /// 0 unknown, 1 renews, 2 ends
    @objc public var renewal = 0
    @objc public var hadSubscription = false
}

@MainActor
@objc(VPStore) public final class VPStore: NSObject {
    @objc public static let shared = VPStore()

    private let ids = ["com.virtualpercussionist.app.pro.monthly",
                       "com.virtualpercussionist.app.pro.yearly"]
    private var observer: ((VPStatus) -> Void)?
    private var products: [Product] = []
    private var plans: [VPPlan] = []
    private var productsLoaded = false
    private var productsFailed = false
    private var updates: Task<Void, Never>?
    private var last = VPStatus()

    @objc public func start(_ onChange: @escaping (VPStatus) -> Void) {
        observer = onChange
        onChange(last)   // "checking": the paywall must not flash before we know
        // Renewals, purchases on another device, refunds, Ask-to-Buy approvals.
        updates = Task { [weak self] in
            for await update in Transaction.updates {
                if case .verified(let t) = update { await t.finish() }
                await self?.refresh()
            }
        }
        Task { await loadProducts(); await refresh() }
    }

    @objc public func reloadProducts() {
        Task { await loadProducts(); await refresh() }
    }

    // MARK: products

    private func loadProducts() async {
        do {
            let found = try await Product.products(for: ids)
            products = found
            var built: [VPPlan] = []
            let monthly = found.first { $0.id == ids[0] }
            for p in found.sorted(by: { $0.id < $1.id }) {   // ".monthly" < ".yearly"
                guard let sub = p.subscription else { continue }
                let plan = VPPlan()
                plan.id = p.id
                plan.price = p.displayPrice
                plan.yearly = sub.subscriptionPeriod.unit == .year
                if let offer = sub.introductoryOffer, offer.paymentMode == .freeTrial {
                    plan.trialDays = Self.days(offer.period)
                    plan.trialEligible = await sub.isEligibleForIntroOffer
                }
                if plan.yearly {
                    plan.pricePerMonth = (p.price / 12).formatted(p.priceFormatStyle)
                    if let m = monthly {
                        let year = NSDecimalNumber(decimal: m.price * 12).doubleValue
                        let paid = NSDecimalNumber(decimal: p.price).doubleValue
                        if year > 0 { plan.savePercent = max(0, Int(((year - paid) / year * 100).rounded())) }
                    }
                }
                built.append(plan)
            }
            plans = built
            productsLoaded = !built.isEmpty
            productsFailed = built.isEmpty
        } catch {
            productsLoaded = false
            productsFailed = true
        }
    }

    private static func days(_ p: Product.SubscriptionPeriod) -> Int {
        switch p.unit {
        case .day: return p.value
        case .week: return p.value * 7
        case .month: return p.value * 30
        case .year: return p.value * 365
        @unknown default: return p.value
        }
    }

    // MARK: entitlement

    private func refresh() async {
        let s = VPStatus()
        s.plans = plans
        s.productsLoaded = productsLoaded
        s.productsFailed = productsFailed
        s.access = 1

        // Only what Apple signed, only our products, nothing refunded or lapsed.
        for await result in Transaction.currentEntitlements {
            guard case .verified(let t) = result, ids.contains(t.productID),
                  t.revocationDate == nil else { continue }
            if let end = t.expirationDate, end < Date() { continue }
            let isTrial: Bool
            if #available(iOS 17.2, *) { isTrial = t.offer?.type == .introductory }
            else { isTrial = t.offerType == .introductory }
            let rank = isTrial ? 2 : 3     // a paid period beats a trial if both show up
            if rank > s.access {
                s.access = rank
                s.activePlanId = t.productID
                s.expiresAt = t.expirationDate?.timeIntervalSince1970 ?? 0
            }
        }

        if s.access >= 2,
           let p = products.first(where: { $0.id == s.activePlanId }),
           let states = try? await p.subscription?.status,
           let st = states.first(where: { $0.state == .subscribed }),
           case .verified(let info) = st.renewalInfo {
            s.renewal = info.willAutoRenew ? 1 : 2
        }

        if s.access == 1 {
            for id in ids {
                if await Transaction.latest(for: id) != nil { s.hadSubscription = true }
            }
        }

        last = s
        observer?(s)
    }

    // MARK: actions

    /// 0 success, 1 pending (Ask to Buy), 2 cancelled, 3 failed
    @objc public func purchase(_ id: String, done: @escaping (Int, String) -> Void) {
        guard let product = products.first(where: { $0.id == id }) else {
            done(3, "Product not available"); return
        }
        Task {
            do {
                switch try await product.purchase() {
                case .success(let verification):
                    guard case .verified(let t) = verification else {
                        done(3, "The purchase could not be verified"); return
                    }
                    await t.finish()
                    await refresh()
                    done(0, "")
                case .pending: done(1, "")
                case .userCancelled: done(2, "")
                @unknown default: done(3, "")
                }
            } catch {
                done(3, error.localizedDescription)
            }
        }
    }

    @objc public func restore(_ done: @escaping (Bool) -> Void) {
        Task {
            do { try await AppStore.sync(); await refresh(); done(true) }
            catch { done(false) }
        }
    }

    @objc public func manage() {
        Task {
            let scene = UIApplication.shared.connectedScenes
                .compactMap { $0 as? UIWindowScene }
                .first { $0.activationState == .foregroundActive }
            if let scene = scene, (try? await AppStore.showManageSubscriptions(in: scene)) != nil { return }
            if let url = URL(string: "https://apps.apple.com/account/subscriptions") {
                await UIApplication.shared.open(url)
            }
        }
    }
}
