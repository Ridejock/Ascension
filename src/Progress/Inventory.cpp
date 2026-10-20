#include "Ascension/Progress/Inventory.h"

namespace Ascension {

namespace {

constexpr i32 kGoldCap = 999999;

const std::string& EmptyId()
{
    static const std::string kEmpty;
    return kEmpty;
}

const ItemStack& EmptyStack()
{
    static const ItemStack kEmpty;
    return kEmpty;
}

} // namespace

bool Inventory::Start(const Database& data, std::string& error)
{
    m_Data = &data;
    if (Shop() == nullptr || Shop()->BagSlots < 1) {
        m_Data = nullptr;
        error = "shop.json did not load a bag";
        return false;
    }
    m_Gold = 0;
    m_Weapon.clear();
    m_Offhand.clear();
    m_Armor.clear();
    m_Accessory.clear();
    m_Bag.assign(static_cast<usize>(Shop()->BagSlots), ItemStack{});
    error.clear();
    return true;
}

bool Inventory::ApplyData(std::string& error)
{
    const ShopDef* shop = Shop();
    if (shop == nullptr || shop->BagSlots < 1) {
        error = "shop.json did not load a bag";
        return false;
    }
    const i32 slots = shop->BagSlots;
    const i32 cap = shop->StackSize < 1 ? 1 : shop->StackSize;
    if (static_cast<i32>(m_Bag.size()) > slots)
        m_Bag.resize(static_cast<usize>(slots));
    while (static_cast<i32>(m_Bag.size()) < slots)
        m_Bag.emplace_back();
    for (ItemStack& stack : m_Bag) {
        if (stack.Count > cap)
            stack.Count = cap;
        if (stack.Count <= 0) {
            stack.Id.clear();
            stack.Count = 0;
        }
    }

    error.clear();
    if (OffhandBlocked() && !m_Offhand.empty()) {
        const std::string piece = m_Offhand;
        std::vector<ItemStack> next = m_Bag;
        bool dropped = false;
        if (!GiveOrDropLast(next, piece, dropped)) {
            error = "The bag had no room. The offhand piece was set down.";
        } else {
            m_Bag = std::move(next);
            if (dropped)
                error = "The bag was full. A stack was set down so the offhand could come off.";
        }
        m_Offhand.clear();
    }
    return true;
}

const ItemStack& Inventory::BagAt(i32 index) const
{
    if (index < 0 || index >= static_cast<i32>(m_Bag.size()))
        return EmptyStack();
    return m_Bag[static_cast<usize>(index)];
}

const std::string& Inventory::Equipped(GearSlot slot) const
{
    if (slot == GearSlot::Weapon)
        return m_Weapon;
    if (slot == GearSlot::Offhand)
        return m_Offhand;
    if (slot == GearSlot::Armor)
        return m_Armor;
    if (slot == GearSlot::Accessory)
        return m_Accessory;
    return EmptyId();
}

bool Inventory::OffhandBlocked() const
{
    if (m_Data == nullptr || m_Weapon.empty())
        return false;
    const ItemDef* weapon = m_Data->FindItem(m_Weapon);
    return weapon != nullptr && weapon->TwoHanded;
}

StatBlock Inventory::GearBonus() const
{
    StatBlock bonus;
    if (m_Data == nullptr)
        return bonus;
    const bool blocked = OffhandBlocked();
    const std::string* worn[] = {&m_Weapon, &m_Offhand, &m_Armor, &m_Accessory};
    for (const std::string* id : worn) {
        if (blocked && id == &m_Offhand)
            continue;
        if (id->empty())
            continue;
        const ItemDef* item = m_Data->FindItem(*id);
        if (item != nullptr)
            bonus += item->Bonus;
    }
    return bonus;
}

std::vector<std::string> Inventory::WornIds() const
{
    std::vector<std::string> ids;
    const bool blocked = OffhandBlocked();
    const std::string* worn[] = {&m_Weapon, &m_Offhand, &m_Armor, &m_Accessory};
    for (const std::string* id : worn) {
        if ((blocked && id == &m_Offhand) || id->empty())
            continue;
        ids.push_back(*id);
    }
    return ids;
}

std::vector<i32> Inventory::ConsumableSlots() const
{
    std::vector<i32> slots;
    if (m_Data == nullptr)
        return slots;
    for (i32 index = 0; index < static_cast<i32>(m_Bag.size()); ++index) {
        const ItemStack& stack = m_Bag[static_cast<usize>(index)];
        if (stack.Empty())
            continue;
        const ItemDef* item = m_Data->FindItem(stack.Id);
        if (item != nullptr && item->Slot == GearSlot::Consumable)
            slots.push_back(index);
    }
    return slots;
}

void Inventory::GrantGold(i32 amount)
{
    if (amount <= 0)
        return;
    i64 next = static_cast<i64>(m_Gold) + amount;
    if (next > kGoldCap)
        next = kGoldCap;
    m_Gold = static_cast<i32>(next);
}

bool Inventory::SetGold(i32 amount, std::string& error)
{
    if (amount < 0) {
        error = "gold cannot be negative";
        return false;
    }
    if (amount > kGoldCap) {
        error = "gold " + std::to_string(amount) + " is above " + std::to_string(kGoldCap);
        return false;
    }
    m_Gold = amount;
    error.clear();
    return true;
}

bool Inventory::Give(std::string_view itemId, i32 count, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the bag is not ready";
        return false;
    }
    if (count < 1) {
        error = "item '" + std::string(itemId) + "' needs a count of at least 1";
        return false;
    }
    const ItemDef* item = m_Data->FindItem(itemId);
    if (item == nullptr) {
        error = "item '" + std::string(itemId) + "' does not exist";
        return false;
    }
    std::vector<ItemStack> next = m_Bag;
    for (i32 remaining = count; remaining > 0; --remaining) {
        if (!TryGive(next, item->Id)) {
            error = "item '" + item->Id + "' does not fit in the bag";
            return false;
        }
    }
    m_Bag = std::move(next);
    error.clear();
    return true;
}

const ShopDef* Inventory::Shop() const
{
    if (m_Data == nullptr || !m_Data->IsLoaded())
        return nullptr;
    return &m_Data->GetShop();
}

i32 Inventory::StackCap() const
{
    const ShopDef* shop = Shop();
    if (shop == nullptr || shop->StackSize < 1)
        return 1;
    return shop->StackSize;
}

bool Inventory::TryGive(std::vector<ItemStack>& bag, std::string_view id) const
{
    const i32 cap = StackCap();
    for (ItemStack& stack : bag) {
        if (stack.Id == id && stack.Count > 0 && stack.Count < cap) {
            ++stack.Count;
            return true;
        }
    }
    for (ItemStack& stack : bag) {
        if (stack.Empty()) {
            stack.Id = std::string(id);
            stack.Count = 1;
            return true;
        }
    }
    return false;
}

bool Inventory::GiveOrDropLast(std::vector<ItemStack>& bag, std::string_view id,
                               bool& dropped) const
{
    if (TryGive(bag, id))
        return true;
    i32 last = -1;
    for (i32 slot = static_cast<i32>(bag.size()) - 1; slot >= 0; --slot) {
        if (!bag[static_cast<usize>(slot)].Empty()) {
            last = slot;
            break;
        }
    }
    if (last < 0)
        return false;
    bag[static_cast<usize>(last)] = ItemStack{};
    dropped = true;
    return TryGive(bag, id);
}

bool Inventory::Buy(std::string_view itemId, std::string& error)
{
    const ShopDef* shop = Shop();
    if (shop == nullptr) {
        error = "the shop is closed";
        return false;
    }
    bool stocked = false;
    for (const std::string& listed : shop->Stock) {
        if (listed == itemId)
            stocked = true;
    }
    if (!stocked) {
        error = shop->Keeper + " does not sell that";
        return false;
    }
    const ItemDef* item = m_Data->FindItem(itemId);
    if (item == nullptr) {
        error = "that item is gone";
        return false;
    }
    if (!ItemIsOffered(*item, m_Defeated)) {
        error = shop->Keeper + " will not sell that yet";
        return false;
    }
    if (m_Gold < item->Value) {
        error = "not enough gold";
        return false;
    }
    std::vector<ItemStack> next = m_Bag;
    if (!TryGive(next, item->Id)) {
        error = "the bag is full";
        return false;
    }
    m_Gold -= item->Value;
    m_Bag = std::move(next);
    error.clear();
    return true;
}

bool Inventory::Sell(i32 bagIndex, std::string& error)
{
    const ShopDef* shop = Shop();
    if (shop == nullptr) {
        error = "the shop is closed";
        return false;
    }
    if (bagIndex < 0 || bagIndex >= static_cast<i32>(m_Bag.size()) || BagAt(bagIndex).Empty()) {
        error = "nothing there to sell";
        return false;
    }
    ItemStack& stack = m_Bag[static_cast<usize>(bagIndex)];
    const ItemDef* item = m_Data->FindItem(stack.Id);
    if (item == nullptr) {
        error = "that item is gone";
        return false;
    }
    const i32 paid = SellPrice(*shop, item->Value);
    --stack.Count;
    if (stack.Count <= 0) {
        stack.Id.clear();
        stack.Count = 0;
    }
    GrantGold(paid);
    error.clear();
    return true;
}

bool Inventory::Equip(i32 bagIndex, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the bag is not ready";
        return false;
    }
    if (bagIndex < 0 || bagIndex >= static_cast<i32>(m_Bag.size()) || BagAt(bagIndex).Empty()) {
        error = "nothing there to equip";
        return false;
    }
    const ItemDef* item = m_Data->FindItem(m_Bag[static_cast<usize>(bagIndex)].Id);
    if (item == nullptr) {
        error = "that item is gone";
        return false;
    }
    if (item->Slot == GearSlot::Consumable) {
        error = "drink it in a fight";
        return false;
    }
    if (item->Slot == GearSlot::Offhand && OffhandBlocked()) {
        error = "a two-handed weapon fills that hand";
        return false;
    }

    std::string displacedWeapon;
    std::string displacedOffhand;
    if (item->Slot == GearSlot::Weapon) {
        displacedWeapon = m_Weapon;
        if (item->TwoHanded)
            displacedOffhand = m_Offhand;
    } else if (item->Slot == GearSlot::Offhand) {
        displacedOffhand = m_Offhand;
    } else if (item->Slot == GearSlot::Armor) {
        displacedWeapon = m_Armor;
    } else {
        displacedWeapon = m_Accessory;
    }

    std::vector<ItemStack> next = m_Bag;
    ItemStack& source = next[static_cast<usize>(bagIndex)];
    const std::string taken = source.Id;
    --source.Count;
    if (source.Count <= 0) {
        source.Id.clear();
        source.Count = 0;
    }
    if (!displacedOffhand.empty() && !TryGive(next, displacedOffhand)) {
        error = "the bag is full";
        return false;
    }
    if (!displacedWeapon.empty() && !TryGive(next, displacedWeapon)) {
        error = "the bag is full";
        return false;
    }

    m_Bag = std::move(next);
    if (item->Slot == GearSlot::Weapon) {
        m_Weapon = taken;
        if (item->TwoHanded)
            m_Offhand.clear();
    } else if (item->Slot == GearSlot::Offhand) {
        m_Offhand = taken;
    } else if (item->Slot == GearSlot::Armor) {
        m_Armor = taken;
    } else {
        m_Accessory = taken;
    }
    error.clear();
    return true;
}

bool Inventory::Unequip(GearSlot slot, std::string& error)
{
    if (slot == GearSlot::Consumable) {
        error = "nothing is worn there";
        return false;
    }
    const std::string worn = Equipped(slot);
    if (worn.empty()) {
        error = "nothing is worn there";
        return false;
    }
    std::vector<ItemStack> next = m_Bag;
    bool dropped = false;
    const bool stored =
        slot == GearSlot::Offhand ? GiveOrDropLast(next, worn, dropped) : TryGive(next, worn);
    if (!stored) {
        error = "the bag is full";
        return false;
    }
    m_Bag = std::move(next);
    if (slot == GearSlot::Weapon)
        m_Weapon.clear();
    else if (slot == GearSlot::Offhand)
        m_Offhand.clear();
    else if (slot == GearSlot::Armor)
        m_Armor.clear();
    else
        m_Accessory.clear();
    if (dropped)
        error = "The bag was full. A stack was set down so the offhand could come off.";
    else
        error.clear();
    return true;
}

bool Inventory::Take(i32 bagIndex, std::string& error)
{
    if (bagIndex < 0 || bagIndex >= static_cast<i32>(m_Bag.size()) || BagAt(bagIndex).Empty()) {
        error = "nothing there to use";
        return false;
    }
    ItemStack& stack = m_Bag[static_cast<usize>(bagIndex)];
    --stack.Count;
    if (stack.Count <= 0) {
        stack.Id.clear();
        stack.Count = 0;
    }
    error.clear();
    return true;
}

void Inventory::SetDefeated(std::vector<std::string> defeated)
{
    m_Defeated = std::move(defeated);
}

void Inventory::ExportBag(BagRecord& out) const
{
    out.Gold = m_Gold;
    out.Bag = m_Bag;
    out.Weapon = m_Weapon;
    out.Offhand = m_Offhand;
    out.Armor = m_Armor;
    out.Accessory = m_Accessory;
}

bool Inventory::ImportBag(const BagRecord& in, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the bag is not ready";
        return false;
    }
    if (!SetGold(in.Gold, error))
        return false;
    m_Weapon = in.Weapon;
    m_Offhand = in.Offhand;
    m_Armor = in.Armor;
    m_Accessory = in.Accessory;
    m_Bag = in.Bag;
    return ApplyData(error);
}

} // namespace Ascension
